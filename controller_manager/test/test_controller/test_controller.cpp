// Copyright 2020 Open Source Robotics Foundation, Inc.
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

#include "test_controller.hpp"

#include <limits>
#include <string>

#include "lifecycle_msgs/msg/state.hpp"

namespace test_controller
{
TestController::TestController()
: controller_interface::ControllerInterface(),
  cmd_iface_cfg_{controller_interface::interface_configuration_type::NONE}
{
  set_first_command_interface_value_to = std::numeric_limits<double>::quiet_NaN();
}

controller_interface::InterfaceConfiguration TestController::command_interface_configuration() const
{
  if (
    get_lifecycle_state().id() == lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE ||
    get_lifecycle_state().id() == lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE)
  {
    return cmd_iface_cfg_;
  }
  else
  {
    throw std::runtime_error(
      "Can not get command interface configuration until the controller is configured.");
  }
}

controller_interface::InterfaceConfiguration TestController::state_interface_configuration() const
{
  if (
    get_lifecycle_state().id() == lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE ||
    get_lifecycle_state().id() == lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE)
  {
    return state_iface_cfg_;
  }
  else
  {
    throw std::runtime_error(
      "Can not get state interface configuration until the controller is configured.");
  }
}

controller_interface::return_type TestController::update(
  const rclcpp::Time & time, const rclcpp::Duration & period)
{
  if (time.get_clock_type() != RCL_ROS_TIME)
  {
    throw std::runtime_error("ROS Time is required for the controller to operate.");
  }
  if (is_async())
  {
    std::this_thread::sleep_for(std::chrono::microseconds(1000000u / (2 * get_update_rate())));
  }
  update_period_ = period;
  ++internal_counter;

  // set value to hardware to produce and test different behaviors there
  if (!std::isnan(set_first_command_interface_value_to))
  {
    (void)command_interfaces_[0].set_value(set_first_command_interface_value_to);
    // reset to be easier to test
    set_first_command_interface_value_to = std::numeric_limits<double>::quiet_NaN();
  }
  else
  {
    for (size_t i = 0; i < command_interfaces_.size(); ++i)
    {
      if (!std::isfinite(external_commands_for_testing_[i]))
      {
        RCLCPP_ERROR(
          get_node()->get_logger(),
          "External command value for command interface '%s' is not finite",
          command_interfaces_[i].get_name().c_str());
        return controller_interface::return_type::ERROR;
      }
      RCLCPP_DEBUG(
        get_node()->get_logger(), "Setting value of command interface '%s' to %f",
        command_interfaces_[i].get_name().c_str(), external_commands_for_testing_[i]);
      (void)command_interfaces_[i].set_value(external_commands_for_testing_[i]);
    }
  }

  return controller_interface::return_type::OK;
}

CallbackReturn TestController::on_init() { return CallbackReturn::SUCCESS; }

CallbackReturn TestController::on_configure(const rclcpp_lifecycle::State & /*previous_state*/)
{
  auto ctrl_node = get_node();
  if (!ctrl_node->has_parameter("command_interfaces"))
  {
    ctrl_node->declare_parameter("command_interfaces", std::vector<std::string>({}));
  }
  if (!ctrl_node->has_parameter("state_interfaces"))
  {
    ctrl_node->declare_parameter("state_interfaces", std::vector<std::string>({}));
  }
  const std::vector<std::string> command_interfaces =
    ctrl_node->get_parameter("command_interfaces").as_string_array();
  const std::vector<std::string> state_interfaces =
    ctrl_node->get_parameter("state_interfaces").as_string_array();
  if (!command_interfaces.empty() || !state_interfaces.empty())
  {
    cmd_iface_cfg_.names.clear();
    state_iface_cfg_.names.clear();
    for (const auto & cmd_itf : command_interfaces)
    {
      cmd_iface_cfg_.names.push_back(cmd_itf);
    }
    cmd_iface_cfg_.type = controller_interface::interface_configuration_type::INDIVIDUAL;
    external_commands_for_testing_.resize(command_interfaces.size(), 0.0);
    for (const auto & state_itf : state_interfaces)
    {
      state_iface_cfg_.names.push_back(state_itf);
    }
    state_iface_cfg_.type = controller_interface::interface_configuration_type::INDIVIDUAL;
  }

  const std::string service_name = get_node()->get_name() + std::string("/set_bool");
  service_ = get_node()->create_service<example_interfaces::srv::SetBool>(
    service_name,
    [this](
      const std::shared_ptr<example_interfaces::srv::SetBool::Request> request,
      std::shared_ptr<example_interfaces::srv::SetBool::Response> response)
    {
      RCLCPP_INFO_STREAM(
        get_node()->get_logger(), "Setting response to " << std::boolalpha << request->data);
      response->success = request->data;
    });

  return CallbackReturn::SUCCESS;
}

CallbackReturn TestController::on_cleanup(const rclcpp_lifecycle::State & /*previous_state*/)
{
  if (simulate_cleanup_failure)
  {
    return CallbackReturn::FAILURE;
  }

  if (cleanup_calls)
  {
    (*cleanup_calls)++;
  }
  return CallbackReturn::SUCCESS;
}

CallbackReturn TestController::on_shutdown(const rclcpp_lifecycle::State &)
{
  if (shutdown_calls)
  {
    (*shutdown_calls)++;
  }
  return CallbackReturn::SUCCESS;
}

void TestController::set_command_interface_configuration(
  const controller_interface::InterfaceConfiguration & cfg)
{
  cmd_iface_cfg_ = cfg;
  external_commands_for_testing_.resize(cmd_iface_cfg_.names.size(), 0.0);
}

void TestController::set_state_interface_configuration(
  const controller_interface::InterfaceConfiguration & cfg)
{
  state_iface_cfg_ = cfg;
}

std::vector<double> TestController::get_state_interface_data() const
{
  std::vector<double> state_intr_data;
  for (const auto & interface : state_interfaces_)
  {
    state_intr_data.push_back(interface.get_optional().value());
  }
  return state_intr_data;
}

void TestController::set_external_commands_for_testing(const std::vector<double> & commands)
{
  external_commands_for_testing_ = commands;
}

}  // namespace test_controller

#include "pluginlib/class_list_macros.hpp"

PLUGINLIB_EXPORT_CLASS(test_controller::TestController, controller_interface::ControllerInterface)
