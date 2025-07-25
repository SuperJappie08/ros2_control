// Copyright 2020 - 2021 ros2_control Development Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef HARDWARE_INTERFACE__ACTUATOR_INTERFACE_HPP_
#define HARDWARE_INTERFACE__ACTUATOR_INTERFACE_HPP_

#include <fmt/compile.h>

#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "hardware_interface/component_parser.hpp"
#include "hardware_interface/handle.hpp"
#include "hardware_interface/hardware_info.hpp"
#include "hardware_interface/introspection.hpp"
#include "hardware_interface/types/hardware_component_interface_params.hpp"
#include "hardware_interface/types/hardware_component_params.hpp"
#include "hardware_interface/types/hardware_interface_return_values.hpp"
#include "hardware_interface/types/lifecycle_state_names.hpp"
#include "hardware_interface/types/trigger_type.hpp"
#include "lifecycle_msgs/msg/state.hpp"
#include "rclcpp/duration.hpp"
#include "rclcpp/logger.hpp"
#include "rclcpp/node_interfaces/node_clock_interface.hpp"
#include "rclcpp/time.hpp"
#include "rclcpp_lifecycle/node_interfaces/lifecycle_node_interface.hpp"
#include "rclcpp_lifecycle/state.hpp"
#include "realtime_tools/async_function_handler.hpp"

namespace hardware_interface
{

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

/// Virtual Class to implement when integrating a 1 DoF actuator into ros2_control.
/**
 * The typical examples are conveyors or motors.
 *
 * Methods return values have type
 * rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn with the following
 * meaning:
 *
 * \returns CallbackReturn::SUCCESS method execution was successful.
 * \returns CallbackReturn::FAILURE method execution has failed and and can be called again.
 * \returns CallbackReturn::ERROR critical error has happened that should be managed in
 * "on_error" method.
 *
 * The hardware ends after each method in a state with the following meaning:
 *
 * UNCONFIGURED (on_init, on_cleanup):
 *   Hardware is initialized but communication is not started and therefore no interface is
 * available.
 *
 * INACTIVE (on_configure, on_deactivate):
 *   Communication with the hardware is started and it is configured.
 *   States can be read and command interfaces are available.
 *
 *    As of now, it is left to the hardware component implementation to continue using the command
 * received from the ``CommandInterfaces`` or to skip them completely.
 *
 * FINALIZED (on_shutdown):
 *   Hardware interface is ready for unloading/destruction.
 *   Allocated memory is cleaned up.
 *
 * ACTIVE (on_activate):
 *   Power circuits of hardware are active and hardware can be moved, e.g., brakes are disabled.
 *   Command interfaces available.
 *
 * \todo
 * Implement
 *  * https://github.com/ros-controls/ros2_control/issues/931
 *  * https://github.com/ros-controls/roadmap/pull/51/files
 *  * this means in INACTIVE state:
 *      * States can be read and non-movement hardware interfaces commanded.
 *      * Hardware interfaces for movement will NOT be available.
 *      * Those interfaces are: HW_IF_POSITION, HW_IF_VELOCITY, HW_IF_ACCELERATION, and
 * HW_IF_EFFORT.
 */

class ActuatorInterface : public rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface
{
public:
  ActuatorInterface()
  : lifecycle_state_(
      rclcpp_lifecycle::State(
        lifecycle_msgs::msg::State::PRIMARY_STATE_UNKNOWN, lifecycle_state_names::UNKNOWN)),
    actuator_logger_(rclcpp::get_logger("actuator_interface"))
  {
  }

  /// ActuatorInterface copy constructor is actively deleted.
  /**
   * Hardware interfaces are having a unique ownership and thus can't be copied in order to avoid
   * failed or simultaneous access to hardware.
   */
  ActuatorInterface(const ActuatorInterface & other) = delete;

  ActuatorInterface(ActuatorInterface && other) = delete;

  virtual ~ActuatorInterface() = default;

  /// Initialization of the hardware interface from data parsed from the robot's URDF and also the
  /// clock and logger interfaces.
  /**
   * \param[in] hardware_info structure with data from URDF.
   * \param[in] clock pointer to the resource manager clock.
   * \param[in] logger Logger for the hardware component.
   * \returns CallbackReturn::SUCCESS if required data are provided and can be parsed.
   * \returns CallbackReturn::ERROR if any error happens or data are missing.
   */
  [[deprecated(
    "Replaced by CallbackReturn init(const hardware_interface::HardwareComponentParams & "
    "params). Initialization is handled by the Framework.")]]
  CallbackReturn init(
    const HardwareInfo & hardware_info, rclcpp::Logger logger, rclcpp::Clock::SharedPtr clock)
  {
    hardware_interface::HardwareComponentParams params;
    params.hardware_info = hardware_info;
    params.clock = clock;
    params.logger = logger;
    return init(params);
  };

  /// Initialization of the hardware interface from data parsed from the robot's URDF and also the
  /// clock and logger interfaces.
  /**
   * \param[in] params  A struct of type HardwareComponentParams containing all necessary
   * parameters for initializing this specific hardware component,
   * including its HardwareInfo, a dedicated logger, a clock, and a
   * weak_ptr to the executor.
   * \warning The parsed executor should not be used to call `cancel()` or use blocking callbacks
   * such as `spin()`.
   * \returns CallbackReturn::SUCCESS if required data are provided and can be parsed.
   * \returns CallbackReturn::ERROR if any error happens or data are missing.
   */
  CallbackReturn init(const hardware_interface::HardwareComponentParams & params)
  {
    actuator_clock_ = params.clock;
    auto logger_copy = params.logger;
    actuator_logger_ =
      logger_copy.get_child("hardware_component.actuator." + params.hardware_info.name);
    info_ = params.hardware_info;
    if (info_.is_async)
    {
      RCLCPP_INFO_STREAM(
        get_logger(), "Starting async handler with scheduler priority: " << info_.thread_priority);
      async_handler_ = std::make_unique<realtime_tools::AsyncFunctionHandler<return_type>>();
      async_handler_->init(
        [this](const rclcpp::Time & time, const rclcpp::Duration & period)
        {
          const auto read_start_time = std::chrono::steady_clock::now();
          const auto ret_read = read(time, period);
          const auto read_end_time = std::chrono::steady_clock::now();
          read_return_info_.store(ret_read, std::memory_order_release);
          read_execution_time_.store(
            std::chrono::duration_cast<std::chrono::nanoseconds>(read_end_time - read_start_time),
            std::memory_order_release);
          if (ret_read != return_type::OK)
          {
            return ret_read;
          }
          const auto write_start_time = std::chrono::steady_clock::now();
          const auto ret_write = write(time, period);
          const auto write_end_time = std::chrono::steady_clock::now();
          write_return_info_.store(ret_write, std::memory_order_release);
          write_execution_time_.store(
            std::chrono::duration_cast<std::chrono::nanoseconds>(write_end_time - write_start_time),
            std::memory_order_release);
          return ret_write;
        },
        info_.thread_priority);
      async_handler_->start_thread();
    }
    hardware_interface::HardwareComponentInterfaceParams interface_params;
    interface_params.hardware_info = info_;
    interface_params.executor = params.executor;
    return on_init(interface_params);
  };

  /// Initialization of the hardware interface from data parsed from the robot's URDF.
  /**
   * \param[in] hardware_info structure with data from URDF.
   * \returns CallbackReturn::SUCCESS if required data are provided and can be parsed.
   * \returns CallbackReturn::ERROR if any error happens or data are missing.
   */
  [[deprecated("Use on_init(const HardwareComponentInterfaceParams & params) instead.")]]
  virtual CallbackReturn on_init(const HardwareInfo & hardware_info)
  {
    info_ = hardware_info;
    parse_state_interface_descriptions(info_.joints, joint_state_interfaces_);
    parse_command_interface_descriptions(info_.joints, joint_command_interfaces_);
    return CallbackReturn::SUCCESS;
  };

  /// Initialization of the hardware interface from data parsed from the robot's URDF.
  /**
   * \param[in] params  A struct of type hardware_interface::HardwareComponentInterfaceParams
   * containing all necessary parameters for initializing this specific hardware component,
   * specifically its HardwareInfo, and a weak_ptr to the executor.
   * \warning The parsed executor should not be used to call `cancel()` or use blocking callbacks
   * such as `spin()`.
   * \returns CallbackReturn::SUCCESS if required data are provided and can be parsed.
   * \returns CallbackReturn::ERROR if any error happens or data are missing.
   */
  virtual CallbackReturn on_init(
    const hardware_interface::HardwareComponentInterfaceParams & params)
  {
    // This is done for backward compatibility with the old on_init method.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    return on_init(params.hardware_info);
#pragma GCC diagnostic pop
  };

  /// Exports all state interfaces for this hardware interface.
  /**
   * Old way of exporting the StateInterfaces. If a empty vector is returned then
   * the on_export_state_interfaces() method is called. If a vector with StateInterfaces is returned
   * then the exporting of the StateInterfaces is only done with this function and the ownership is
   * transferred to the resource manager. The set_command(...), get_command(...), ..., can then not
   * be used.
   *
   * Note the ownership over the state interfaces is transferred to the caller.
   *
   * \return vector of state interfaces
   */
  [[deprecated(
    "Replaced by vector<StateInterface::ConstSharedPtr> on_export_state_interfaces() method. "
    "Exporting is handled by the Framework.")]]
  virtual std::vector<StateInterface> export_state_interfaces()
  {
    // return empty vector by default. For backward compatibility we try calling
    // export_state_interfaces() and only when empty vector is returned call
    // on_export_state_interfaces()
    return {};
  }

  /**
   * Override this method to export custom StateInterfaces which are not defined in the URDF file.
   * Those interfaces will be added to the unlisted_state_interfaces_ map.
   *
   * \return vector of descriptions to the unlisted StateInterfaces
   */
  virtual std::vector<hardware_interface::InterfaceDescription>
  export_unlisted_state_interface_descriptions()
  {
    // return empty vector by default.
    return {};
  }

  /**
   * Default implementation for exporting the StateInterfaces. The StateInterfaces are created
   * according to the InterfaceDescription. The memory accessed by the controllers and hardware is
   * assigned here and resides in the actuator_interface.
   *
   * \return vector of shared pointers to the created and stored StateInterfaces
   */
  virtual std::vector<StateInterface::ConstSharedPtr> on_export_state_interfaces()
  {
    // import the unlisted interfaces
    std::vector<hardware_interface::InterfaceDescription> unlisted_interface_descriptions =
      export_unlisted_state_interface_descriptions();

    std::vector<StateInterface::ConstSharedPtr> state_interfaces;
    state_interfaces.reserve(
      unlisted_interface_descriptions.size() + joint_state_interfaces_.size());

    // add InterfaceDescriptions and create the StateInterfaces from the descriptions and add to
    // maps.
    for (const auto & description : unlisted_interface_descriptions)
    {
      auto name = description.get_name();
      unlisted_state_interfaces_.insert(std::make_pair(name, description));
      auto state_interface = std::make_shared<StateInterface>(description);
      actuator_states_.insert(std::make_pair(name, state_interface));
      unlisted_states_.push_back(state_interface);
      state_interfaces.push_back(std::const_pointer_cast<const StateInterface>(state_interface));
    }

    for (const auto & [name, descr] : joint_state_interfaces_)
    {
      auto state_interface = std::make_shared<StateInterface>(descr);
      actuator_states_.insert(std::make_pair(name, state_interface));
      joint_states_.push_back(state_interface);
      state_interfaces.push_back(std::const_pointer_cast<const StateInterface>(state_interface));
    }
    return state_interfaces;
  }

  /// Exports all command interfaces for this hardware interface.
  /**
   * Old way of exporting the CommandInterfaces. If a empty vector is returned then
   * the on_export_command_interfaces() method is called. If a vector with CommandInterfaces is
   * returned then the exporting of the CommandInterfaces is only done with this function and the
   * ownership is transferred to the resource manager. The set_command(...), get_command(...), ...,
   * can then not be used.
   *
   * Note the ownership over the state interfaces is transferred to the caller.
   *
   * \return vector of state interfaces
   */
  [[deprecated(
    "Replaced by vector<CommandInterface::SharedPtr> on_export_command_interfaces() method. "
    "Exporting is handled by the Framework.")]]
  virtual std::vector<CommandInterface> export_command_interfaces()
  {
    // return empty vector by default. For backward compatibility we try calling
    // export_command_interfaces() and only when empty vector is returned call
    // on_export_command_interfaces()
    return {};
  }

  /**
   * Override this method to export custom CommandInterfaces which are not defined in the URDF file.
   * Those interfaces will be added to the unlisted_command_interfaces_ map.
   *
   * \return vector of descriptions to the unlisted CommandInterfaces
   */
  virtual std::vector<hardware_interface::InterfaceDescription>
  export_unlisted_command_interface_descriptions()
  {
    // return empty vector by default.
    return {};
  }

  /**
   * Default implementation for exporting the CommandInterfaces. The CommandInterfaces are created
   * according to the InterfaceDescription. The memory accessed by the controllers and hardware is
   * assigned here and resides in the actuator_interface.
   *
   * \return vector of shared pointers to the created and stored CommandInterfaces
   */
  virtual std::vector<CommandInterface::SharedPtr> on_export_command_interfaces()
  {
    // import the unlisted interfaces
    std::vector<hardware_interface::InterfaceDescription> unlisted_interface_descriptions =
      export_unlisted_command_interface_descriptions();

    std::vector<CommandInterface::SharedPtr> command_interfaces;
    command_interfaces.reserve(
      unlisted_interface_descriptions.size() + joint_command_interfaces_.size());

    // add InterfaceDescriptions and create the CommandInterfaces from the descriptions and add to
    // maps.
    for (const auto & description : unlisted_interface_descriptions)
    {
      auto name = description.get_name();
      unlisted_command_interfaces_.insert(std::make_pair(name, description));
      auto command_interface = std::make_shared<CommandInterface>(description);
      actuator_commands_.insert(std::make_pair(name, command_interface));
      unlisted_commands_.push_back(command_interface);
      command_interfaces.push_back(command_interface);
    }

    for (const auto & [name, descr] : joint_command_interfaces_)
    {
      auto command_interface = std::make_shared<CommandInterface>(descr);
      actuator_commands_.insert(std::make_pair(name, command_interface));
      joint_commands_.push_back(command_interface);
      command_interfaces.push_back(command_interface);
    }

    return command_interfaces;
  }
  /// Prepare for a new command interface switch.
  /**
   * Prepare for any mode-switching required by the new command interface combination.
   *
   * \note This is a non-realtime evaluation of whether a set of command interface claims are
   * possible, and call to start preparing data structures for the upcoming switch that will occur.
   * \param[in] start_interfaces vector of string identifiers for the command interfaces starting.
   * \param[in] stop_interfaces vector of string identifiers for the command interfaces stopping.
   * \return return_type::OK if the new command interface combination can be prepared (or) if the
   * interface key is not relevant to this actuator. Returns return_type::ERROR otherwise.
   */
  virtual return_type prepare_command_mode_switch(
    const std::vector<std::string> & /*start_interfaces*/,
    const std::vector<std::string> & /*stop_interfaces*/)
  {
    return return_type::OK;
  }

  // Perform switching to the new command interface.
  /**
   * Perform the mode-switching for the new command interface combination.
   *
   * \note This is part of the realtime update loop, and should be fast.
   * \param[in] start_interfaces vector of string identifiers for the command interfaces starting.
   * \param[in] stop_interfaces vector of string identifiers for the command interfaces stopping.
   * \return return_type::OK if the new command interface combination can be switched to (or) if the
   * interface key is not relevant to this actuator. Returns return_type::ERROR otherwise.
   */
  virtual return_type perform_command_mode_switch(
    const std::vector<std::string> & /*start_interfaces*/,
    const std::vector<std::string> & /*stop_interfaces*/)
  {
    return return_type::OK;
  }

  /// Triggers the read method synchronously or asynchronously depending on the HardwareInfo
  /**
   * The data readings from the physical hardware has to be updated
   * and reflected accordingly in the exported state interfaces.
   * That is, the data pointed by the interfaces shall be updated.
   * The method is called in the resource_manager's read loop
   *
   * \param[in] time The time at the start of this control loop iteration
   * \param[in] period The measured time taken by the last control loop iteration
   * \return return_type::OK if the read was successful, return_type::ERROR otherwise.
   */
  HardwareComponentCycleStatus trigger_read(
    const rclcpp::Time & time, const rclcpp::Duration & period)
  {
    HardwareComponentCycleStatus status;
    status.result = return_type::ERROR;
    if (info_.is_async)
    {
      status.result = read_return_info_.load(std::memory_order_acquire);
      const auto read_exec_time = read_execution_time_.load(std::memory_order_acquire);
      if (read_exec_time.count() > 0)
      {
        status.execution_time = read_exec_time;
      }
      const auto result = async_handler_->trigger_async_callback(time, period);
      status.successful = result.first;
      if (!status.successful)
      {
        RCLCPP_WARN(
          get_logger(),
          "Trigger read/write called while the previous async trigger is still in progress for "
          "hardware interface : '%s'. Failed to trigger read/write cycle!",
          info_.name.c_str());
        status.result = return_type::OK;
        return status;
      }
    }
    else
    {
      const auto start_time = std::chrono::steady_clock::now();
      status.successful = true;
      status.result = read(time, period);
      status.execution_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start_time);
    }
    return status;
  }

  /// Read the current state values from the actuator.
  /**
   * The data readings from the physical hardware has to be updated
   * and reflected accordingly in the exported state interfaces.
   * That is, the data pointed by the interfaces shall be updated.
   *
   * \param[in] time The time at the start of this control loop iteration
   * \param[in] period The measured time taken by the last control loop iteration
   * \return return_type::OK if the read was successful, return_type::ERROR otherwise.
   */
  virtual return_type read(const rclcpp::Time & time, const rclcpp::Duration & period) = 0;

  /// Triggers the write method synchronously or asynchronously depending on the HardwareInfo
  /**
   * The physical hardware shall be updated with the latest value from
   * the exported command interfaces.
   * The method is called in the resource_manager's write loop
   *
   * \param[in] time The time at the start of this control loop iteration
   * \param[in] period The measured time taken by the last control loop iteration
   * \return return_type::OK if the read was successful, return_type::ERROR otherwise.
   */
  HardwareComponentCycleStatus trigger_write(
    const rclcpp::Time & time, const rclcpp::Duration & period)
  {
    HardwareComponentCycleStatus status;
    status.result = return_type::ERROR;
    if (info_.is_async)
    {
      status.successful = true;
      const auto write_exec_time = write_execution_time_.load(std::memory_order_acquire);
      if (write_exec_time.count() > 0)
      {
        status.execution_time = write_exec_time;
      }
      status.result = write_return_info_.load(std::memory_order_acquire);
    }
    else
    {
      const auto start_time = std::chrono::steady_clock::now();
      status.successful = true;
      status.result = write(time, period);
      status.execution_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start_time);
    }
    return status;
  }

  /// Write the current command values to the actuator.
  /**
   * The physical hardware shall be updated with the latest value from
   * the exported command interfaces.
   *
   * \param[in] time The time at the start of this control loop iteration
   * \param[in] period The measured time taken by the last control loop iteration
   * \return return_type::OK if the read was successful, return_type::ERROR otherwise.
   */
  virtual return_type write(const rclcpp::Time & time, const rclcpp::Duration & period) = 0;

  /// Get name of the actuator hardware.
  /**
   * \return name.
   */
  const std::string & get_name() const { return info_.name; }

  /// Get name of the actuator hardware group to which it belongs to.
  /**
   * \return group name.
   */
  const std::string & get_group_name() const { return info_.group; }

  /// Get life-cycle state of the actuator hardware.
  /**
   * \return state.
   */
  const rclcpp_lifecycle::State & get_lifecycle_state() const { return lifecycle_state_; }

  /// Set life-cycle state of the actuator hardware.
  /**
   * \return state.
   */
  void set_lifecycle_state(const rclcpp_lifecycle::State & new_state)
  {
    lifecycle_state_ = new_state;
  }

  template <typename T>
  void set_state(const std::string & interface_name, const T & value)
  {
    auto it = actuator_states_.find(interface_name);
    if (it == actuator_states_.end())
    {
      throw std::runtime_error(
        fmt::format(
          FMT_COMPILE(
            "State interface not found: {} in actuator hardware component: {}. "
            "This should not happen."),
          interface_name, info_.name));
    }
    auto & handle = it->second;
    std::unique_lock<std::shared_mutex> lock(handle->get_mutex());
    std::ignore = handle->set_value(lock, value);
  }

  template <typename T = double>
  T get_state(const std::string & interface_name) const
  {
    auto it = actuator_states_.find(interface_name);
    if (it == actuator_states_.end())
    {
      throw std::runtime_error(
        fmt::format(
          FMT_COMPILE(
            "State interface not found: {} in actuator hardware component: {}. "
            "This should not happen."),
          interface_name, info_.name));
    }
    auto & handle = it->second;
    std::shared_lock<std::shared_mutex> lock(handle->get_mutex());
    const auto opt_value = handle->get_optional<T>(lock);
    if (!opt_value)
    {
      throw std::runtime_error(
        fmt::format(
          FMT_COMPILE("Failed to get state value from interface: {}. This should not happen."),
          interface_name));
    }
    return opt_value.value();
  }

  template <typename T>
  void set_command(const std::string & interface_name, const T & value)
  {
    auto it = actuator_commands_.find(interface_name);
    if (it == actuator_commands_.end())
    {
      throw std::runtime_error(
        fmt::format(
          FMT_COMPILE(
            "Command interface not found: {} in actuator hardware component: {}. "
            "This should not happen."),
          interface_name, info_.name));
    }
    auto & handle = it->second;
    std::unique_lock<std::shared_mutex> lock(handle->get_mutex());
    std::ignore = handle->set_value(lock, value);
  }

  template <typename T = double>
  T get_command(const std::string & interface_name) const
  {
    auto it = actuator_commands_.find(interface_name);
    if (it == actuator_commands_.end())
    {
      throw std::runtime_error(
        fmt::format(
          FMT_COMPILE(
            "Command interface not found: {} in actuator hardware component: {}. "
            "This should not happen."),
          interface_name, info_.name));
    }
    auto & handle = it->second;
    std::shared_lock<std::shared_mutex> lock(handle->get_mutex());
    const auto opt_value = handle->get_optional<double>(lock);
    if (!opt_value)
    {
      throw std::runtime_error(
        fmt::format(
          FMT_COMPILE("Failed to get command value from interface: {}. This should not happen."),
          interface_name));
    }
    return opt_value.value();
  }

  /// Get the logger of the ActuatorInterface.
  /**
   * \return logger of the ActuatorInterface.
   */
  rclcpp::Logger get_logger() const { return actuator_logger_; }

  /// Get the clock of the ActuatorInterface.
  /**
   * \return clock of the ActuatorInterface.
   */
  rclcpp::Clock::SharedPtr get_clock() const { return actuator_clock_; }

  /// Get the hardware info of the ActuatorInterface.
  /**
   * \return hardware info of the ActuatorInterface.
   */
  const HardwareInfo & get_hardware_info() const { return info_; }

  /// Prepare for the activation of the hardware.
  /**
   * This method is called before the hardware is activated by the resource manager.
   */
  void prepare_for_activation()
  {
    read_return_info_.store(return_type::OK, std::memory_order_release);
    read_execution_time_.store(std::chrono::nanoseconds::zero(), std::memory_order_release);
    write_return_info_.store(return_type::OK, std::memory_order_release);
    write_execution_time_.store(std::chrono::nanoseconds::zero(), std::memory_order_release);
  }

  /// Enable or disable introspection of the hardware.
  /**
   * \param[in] enable Enable introspection if true, disable otherwise.
   */
  void enable_introspection(bool enable)
  {
    if (enable)
    {
      stats_registrations_.enableAll();
    }
    else
    {
      stats_registrations_.disableAll();
    }
  }

protected:
  HardwareInfo info_;
  // interface names to InterfaceDescription
  std::unordered_map<std::string, InterfaceDescription> joint_state_interfaces_;
  std::unordered_map<std::string, InterfaceDescription> joint_command_interfaces_;

  std::unordered_map<std::string, InterfaceDescription> unlisted_state_interfaces_;
  std::unordered_map<std::string, InterfaceDescription> unlisted_command_interfaces_;

  // Exported Command- and StateInterfaces in order they are listed in the hardware description.
  std::vector<StateInterface::SharedPtr> joint_states_;
  std::vector<CommandInterface::SharedPtr> joint_commands_;

  std::vector<StateInterface::SharedPtr> unlisted_states_;
  std::vector<CommandInterface::SharedPtr> unlisted_commands_;

  rclcpp_lifecycle::State lifecycle_state_;
  std::unique_ptr<realtime_tools::AsyncFunctionHandler<return_type>> async_handler_;

private:
  rclcpp::Clock::SharedPtr actuator_clock_;
  rclcpp::Logger actuator_logger_;
  // interface names to Handle accessed through getters/setters
  std::unordered_map<std::string, StateInterface::SharedPtr> actuator_states_;
  std::unordered_map<std::string, CommandInterface::SharedPtr> actuator_commands_;
  std::atomic<return_type> read_return_info_ = return_type::OK;
  std::atomic<std::chrono::nanoseconds> read_execution_time_ = std::chrono::nanoseconds::zero();
  std::atomic<return_type> write_return_info_ = return_type::OK;
  std::atomic<std::chrono::nanoseconds> write_execution_time_ = std::chrono::nanoseconds::zero();

protected:
  pal_statistics::RegistrationsRAII stats_registrations_;
};

}  // namespace hardware_interface
#endif  // HARDWARE_INTERFACE__ACTUATOR_INTERFACE_HPP_
