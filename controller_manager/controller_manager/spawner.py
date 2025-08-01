#!/usr/bin/env python3
# Copyright 2021 PAL Robotics S.L.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import errno
import os
import sys
import time
import warnings

from controller_manager import (
    configure_controller,
    list_controllers,
    load_controller,
    switch_controllers,
    unload_controller,
    set_controller_parameters,
    set_controller_parameters_from_param_files,
    bcolors,
)
from controller_manager_msgs.srv import SwitchController
from controller_manager.controller_manager_services import ServiceNotFoundError

from filelock import Timeout, FileLock
import rclpy
from rclpy.node import Node
from rclpy.signals import SignalHandlerOptions


def first_match(iterable, predicate):
    return next((n for n in iterable if predicate(n)), None)


def combine_name_and_namespace(name_and_namespace):
    node_name, namespace = name_and_namespace
    return namespace + ("" if namespace.endswith("/") else "/") + node_name


def find_node_and_namespace(node, full_node_name):
    node_names_and_namespaces = node.get_node_names_and_namespaces()
    return first_match(
        node_names_and_namespaces, lambda n: combine_name_and_namespace(n) == full_node_name
    )


def has_service_names(node, node_name, node_namespace, service_names):
    client_names_and_types = node.get_service_names_and_types_by_node(node_name, node_namespace)
    if not client_names_and_types:
        return False
    client_names, _ = zip(*client_names_and_types)
    return all(service in client_names for service in service_names)


def is_controller_loaded(
    node, controller_manager, controller_name, service_timeout=0.0, call_timeout=10.0
):
    controllers = list_controllers(
        node, controller_manager, service_timeout, call_timeout
    ).controller
    return any(c.name == controller_name for c in controllers)


def main(args=None):
    rclpy.init(args=args, signal_handler_options=SignalHandlerOptions.NO)
    parser = argparse.ArgumentParser()
    parser.add_argument("controller_names", help="List of controllers", nargs="+")
    parser.add_argument(
        "-c",
        "--controller-manager",
        help="Name of the controller manager ROS node",
        default="controller_manager",
        required=False,
    )
    parser.add_argument(
        "-p",
        "--param-file",
        help="Controller param file to be loaded into controller node before configure. "
        "Pass multiple times to load different files for different controllers or to "
        "override the parameters of the same controller.",
        default=None,
        action="append",
        required=False,
    )
    parser.add_argument(
        "--load-only",
        help="Only load the controller and leave unconfigured.",
        action="store_true",
        required=False,
    )
    parser.add_argument(
        "--inactive",
        help="Load and configure the controller, however do not activate them",
        action="store_true",
        required=False,
    )
    parser.add_argument(
        "-u",
        "--unload-on-kill",
        help="Wait until this application is interrupted and unload controller",
        action="store_true",
    )
    parser.add_argument(
        "--controller-manager-timeout",
        help="Time to wait for the controller manager service to be available",
        required=False,
        default=0.0,
        type=float,
    )
    parser.add_argument(
        "--switch-timeout",
        help="Time to wait for a successful state switch of controllers."
        " Useful when switching cannot be performed immediately, e.g.,"
        " paused simulations at startup",
        required=False,
        default=5.0,
        type=float,
    )
    parser.add_argument(
        "--service-call-timeout",
        help="Time to wait for the service response from the controller manager",
        required=False,
        default=10.0,
        type=float,
    )
    parser.add_argument(
        "--activate-as-group",
        help="Activates all the parsed controllers list together instead of one by one."
        " Useful for activating all chainable controllers altogether",
        action="store_true",
        required=False,
    )
    parser.add_argument(
        "--controller-ros-args",
        help="The --ros-args to be passed to the controller node, e.g., for remapping topics. "
        "Pass multiple times for every argument.",
        default=None,
        action="append",
        required=False,
    )

    command_line_args = rclpy.utilities.remove_ros_args(args=sys.argv)[1:]
    args = parser.parse_args(command_line_args)
    controller_names = args.controller_names
    controller_manager_name = args.controller_manager
    param_files = args.param_file
    controller_manager_timeout = args.controller_manager_timeout
    service_call_timeout = args.service_call_timeout
    switch_timeout = args.switch_timeout
    strictness = SwitchController.Request.STRICT
    unload_controllers_upon_exit = False
    node = None

    if param_files:
        for param_file in param_files:
            if not os.path.isfile(param_file):
                raise FileNotFoundError(errno.ENOENT, os.strerror(errno.ENOENT), param_file)
    logger = rclpy.logging.get_logger("ros2_control_controller_spawner_" + controller_names[0])

    try:
        spawner_node_name = "spawner_" + controller_names[0]
        lock = FileLock("/tmp/ros2-control-controller-spawner.lock")
        max_retries = 5
        retry_delay = 3  # seconds
        for attempt in range(max_retries):
            try:
                logger.debug(
                    bcolors.OKGREEN + "Waiting for the spawner lock to be acquired!" + bcolors.ENDC
                )
                # timeout after 20 seconds and try again
                lock.acquire(timeout=20)
                logger.debug(bcolors.OKGREEN + "Spawner lock acquired!" + bcolors.ENDC)
                break
            except Timeout:
                logger.warning(
                    bcolors.WARNING
                    + f"Attempt {attempt+1} failed. Retrying in {retry_delay} seconds..."
                    + bcolors.ENDC
                )
                time.sleep(retry_delay)
        else:
            logger.error(
                bcolors.FAIL + "Failed to acquire lock after multiple attempts." + bcolors.ENDC
            )
            return 1

        node = Node(spawner_node_name)
        logger = node.get_logger()

        spawner_namespace = node.get_namespace()

        if not spawner_namespace.startswith("/"):
            spawner_namespace = f"/{spawner_namespace}"

        if not controller_manager_name.startswith("/"):
            if spawner_namespace and spawner_namespace != "/":
                controller_manager_name = f"{spawner_namespace}/{controller_manager_name}"
            else:
                controller_manager_name = f"/{controller_manager_name}"

        for controller_name in controller_names:

            if is_controller_loaded(
                node,
                controller_manager_name,
                controller_name,
                controller_manager_timeout,
                service_call_timeout,
            ):
                logger.warning(
                    bcolors.WARNING
                    + "Controller already loaded, skipping load_controller"
                    + bcolors.ENDC
                )
            else:
                if controller_ros_args := args.controller_ros_args:
                    if not set_controller_parameters(
                        node,
                        controller_manager_name,
                        controller_name,
                        "node_options_args",
                        [arg for args in controller_ros_args for arg in args.split()],
                    ):
                        return 1
                if param_files:
                    if not set_controller_parameters_from_param_files(
                        node,
                        controller_manager_name,
                        controller_name,
                        param_files,
                        spawner_namespace,
                    ):
                        return 1

                ret = load_controller(
                    node,
                    controller_manager_name,
                    controller_name,
                    controller_manager_timeout,
                    service_call_timeout,
                )
                if not ret.ok:
                    logger.fatal(
                        bcolors.FAIL
                        + "Failed loading controller "
                        + bcolors.BOLD
                        + controller_name
                        + bcolors.ENDC
                    )
                    return 1
                logger.info(
                    bcolors.OKBLUE + "Loaded " + bcolors.BOLD + controller_name + bcolors.ENDC
                )

            if not args.load_only:
                ret = configure_controller(
                    node,
                    controller_manager_name,
                    controller_name,
                    controller_manager_timeout,
                    service_call_timeout,
                )
                if not ret.ok:
                    logger.error(bcolors.FAIL + "Failed to configure controller" + bcolors.ENDC)
                    return 1

                if not args.inactive and not args.activate_as_group:
                    ret = switch_controllers(
                        node,
                        controller_manager_name,
                        [],
                        [controller_name],
                        strictness,
                        True,
                        switch_timeout,
                        service_call_timeout,
                    )
                    if not ret.ok:
                        logger.error(
                            f"{bcolors.FAIL}Failed to activate controller : {controller_name}{bcolors.ENDC}"
                        )
                        return 1

                    logger.info(
                        bcolors.OKGREEN
                        + "Configured and activated "
                        + bcolors.BOLD
                        + controller_name
                        + bcolors.ENDC
                    )

        if not args.inactive and args.activate_as_group:
            ret = switch_controllers(
                node,
                controller_manager_name,
                [],
                controller_names,
                strictness,
                True,
                switch_timeout,
                service_call_timeout,
            )
            if not ret.ok:
                logger.error(
                    f"{bcolors.FAIL}Failed to activate the parsed controllers list : {controller_names}{bcolors.ENDC}"
                )
                return 1

            logger.info(
                bcolors.OKGREEN
                + f"Configured and activated all the parsed controllers list : {controller_names}!"
                + bcolors.ENDC
            )
        unload_controllers_upon_exit = args.unload_on_kill
        if not unload_controllers_upon_exit:
            return 0

        logger.info("Waiting until interrupt to unload controllers")
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        if unload_controllers_upon_exit:
            logger.info("KeyboardInterrupt successfully captured!")
            if not args.inactive:
                logger.info("Deactivating and unloading controllers...")
                # TODO(saikishor) we might have an issue in future, if any of these controllers is in chained mode
                ret = switch_controllers(
                    node,
                    controller_manager_name,
                    controller_names,
                    [],
                    strictness,
                    True,
                    switch_timeout,
                    service_call_timeout,
                )
                if not ret.ok:
                    logger.error(bcolors.FAIL + "Failed to deactivate controller" + bcolors.ENDC)
                    return 1

                logger.info(f"Successfully deactivated controllers : {controller_names}")

            unload_status = True
            for controller_name in controller_names:
                ret = unload_controller(
                    node,
                    controller_manager_name,
                    controller_name,
                    controller_manager_timeout,
                    service_call_timeout,
                )
                if not ret.ok:
                    unload_status = False
                    logger.error(
                        bcolors.FAIL
                        + f"Failed to unload controller : {controller_name}"
                        + bcolors.ENDC
                    )

            if unload_status:
                logger.info(f"Successfully unloaded controllers : {controller_names}")
            else:
                return 1
        else:
            logger.info("KeyboardInterrupt received! Exiting....")
            pass
    except ServiceNotFoundError as err:
        logger.fatal(str(err))
        return 1
    finally:
        if node:
            node.destroy_node()
        lock.release()
        rclpy.shutdown()


if __name__ == "__main__":
    warnings.warn(
        "'spawner.py' is deprecated, please use 'spawner' (without .py extension)",
        DeprecationWarning,
    )
    ret = main()
    sys.exit(ret)
