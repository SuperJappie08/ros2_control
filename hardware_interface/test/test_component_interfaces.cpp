// Copyright 2020 ros2_control development team
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

#include <array>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "gmock/gmock.h"
#include "hardware_interface/actuator.hpp"
#include "hardware_interface/actuator_interface.hpp"
#include "hardware_interface/hardware_info.hpp"
#include "hardware_interface/sensor.hpp"
#include "hardware_interface/sensor_interface.hpp"
#include "hardware_interface/system.hpp"
#include "hardware_interface/system_interface.hpp"
#include "hardware_interface/types/hardware_interface_return_values.hpp"
#include "hardware_interface/types/hardware_interface_type_values.hpp"
#include "hardware_interface/types/lifecycle_state_names.hpp"
#include "lifecycle_msgs/msg/state.hpp"
#include "rclcpp/node.hpp"
#include "rclcpp_lifecycle/node_interfaces/lifecycle_node_interface.hpp"
#include "ros2_control_test_assets/components_urdfs.hpp"
#include "ros2_control_test_assets/descriptions.hpp"
#include "test_components.hpp"

// Values to send over command interface to trigger error in write and read methods

namespace
{
const auto TIME = rclcpp::Time(0);
const auto PERIOD = rclcpp::Duration::from_seconds(0.01);
constexpr unsigned int TRIGGER_READ_WRITE_ERROR_CALLS = 10000;
}  // namespace

using namespace ::testing;  // NOLINT

namespace test_components
{
using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

// BEGIN (Handle export change): for backward compatibility
class DummyActuator : public hardware_interface::ActuatorInterface
{
  CallbackReturn on_init(
    const hardware_interface::HardwareComponentInterfaceParams & /*params*/) override
  {
    // We hardcode the params
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State & /*previous_state*/) override
  {
    position_state_ = 0.0;
    velocity_state_ = 0.0;

    if (recoverable_error_happened_)
    {
      velocity_command_ = 0.0;
    }

    read_calls_ = 0;
    write_calls_ = 0;

    return CallbackReturn::SUCCESS;
  }

  std::vector<hardware_interface::StateInterface> export_state_interfaces() override
  {
    // We can read a position and a velocity
    std::vector<hardware_interface::StateInterface> state_interfaces;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    state_interfaces.emplace_back(
      hardware_interface::StateInterface(
        "joint1", hardware_interface::HW_IF_POSITION, &position_state_));
    state_interfaces.emplace_back(
      hardware_interface::StateInterface(
        "joint1", hardware_interface::HW_IF_VELOCITY, &velocity_state_));
#pragma GCC diagnostic pop
    return state_interfaces;
  }

  std::vector<hardware_interface::CommandInterface> export_command_interfaces() override
  {
    // We can command in velocity
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    std::vector<hardware_interface::CommandInterface> command_interfaces;
    command_interfaces.emplace_back(
      hardware_interface::CommandInterface(
        "joint1", hardware_interface::HW_IF_VELOCITY, &velocity_command_));
#pragma GCC diagnostic pop
    return command_interfaces;
  }

  hardware_interface::return_type read(
    const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/) override
  {
    ++read_calls_;
    if (read_calls_ == TRIGGER_READ_WRITE_ERROR_CALLS)
    {
      return hardware_interface::return_type::ERROR;
    }

    // no-op, state is getting propagated within write.
    return hardware_interface::return_type::OK;
  }

  hardware_interface::return_type write(
    const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/) override
  {
    ++write_calls_;
    if (write_calls_ == TRIGGER_READ_WRITE_ERROR_CALLS)
    {
      return hardware_interface::return_type::ERROR;
    }

    position_state_ += velocity_command_;
    velocity_state_ = velocity_command_;

    return hardware_interface::return_type::OK;
  }

  CallbackReturn on_shutdown(const rclcpp_lifecycle::State & /*previous_state*/) override
  {
    velocity_state_ = 0;
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_error(const rclcpp_lifecycle::State & /*previous_state*/) override
  {
    if (!recoverable_error_happened_)
    {
      recoverable_error_happened_ = true;
      return CallbackReturn::SUCCESS;
    }
    else
    {
      return CallbackReturn::ERROR;
    }
    return CallbackReturn::FAILURE;
  }

private:
  double position_state_ = std::numeric_limits<double>::quiet_NaN();
  double velocity_state_ = std::numeric_limits<double>::quiet_NaN();
  double velocity_command_ = 0.0;

  // Helper variables to initiate error on read
  unsigned int read_calls_ = 0;
  unsigned int write_calls_ = 0;
  bool recoverable_error_happened_ = false;
};
// END

class DummyActuatorDefault : public hardware_interface::ActuatorInterface
{
  CallbackReturn on_init(
    const hardware_interface::HardwareComponentInterfaceParams & params) override
  {
    // We hardcode the params
    if (
      hardware_interface::ActuatorInterface::on_init(params) !=
      hardware_interface::CallbackReturn::SUCCESS)
    {
      return hardware_interface::CallbackReturn::ERROR;
    }
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State & /*previous_state*/) override
  {
    set_state("joint1/position", 0.0);
    set_state("joint1/velocity", 0.0);

    if (recoverable_error_happened_)
    {
      set_command("joint1/velocity", 0.0);
    }
    // Should throw as the interface is unknown
    EXPECT_THROW(get_state("joint1/nonexisting/interface"), std::runtime_error);
    EXPECT_THROW(get_command("joint1/nonexisting/interface"), std::runtime_error);
    EXPECT_THROW(set_state("joint1/nonexisting/interface", 0.0), std::runtime_error);
    EXPECT_THROW(set_command("joint1/nonexisting/interface", 0.0), std::runtime_error);

    read_calls_ = 0;
    write_calls_ = 0;

    return CallbackReturn::SUCCESS;
  }

  hardware_interface::return_type read(
    const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/) override
  {
    ++read_calls_;
    if (read_calls_ == TRIGGER_READ_WRITE_ERROR_CALLS)
    {
      return hardware_interface::return_type::ERROR;
    }

    // no-op, state is getting propagated within write.
    return hardware_interface::return_type::OK;
  }

  hardware_interface::return_type write(
    const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/) override
  {
    ++write_calls_;
    if (write_calls_ == TRIGGER_READ_WRITE_ERROR_CALLS)
    {
      return hardware_interface::return_type::ERROR;
    }
    auto position_state = get_state("joint1/position");
    set_state("joint1/position", position_state + get_command("joint1/velocity"));
    set_state("joint1/velocity", get_command("joint1/velocity"));

    return hardware_interface::return_type::OK;
  }

  CallbackReturn on_shutdown(const rclcpp_lifecycle::State & /*previous_state*/) override
  {
    set_state("joint1/velocity", 0.0);
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_error(const rclcpp_lifecycle::State & /*previous_state*/) override
  {
    if (!recoverable_error_happened_)
    {
      recoverable_error_happened_ = true;
      return CallbackReturn::SUCCESS;
    }
    else
    {
      return CallbackReturn::ERROR;
    }
    return CallbackReturn::FAILURE;
  }

private:
  // Helper variables to initiate error on read
  unsigned int read_calls_ = 0;
  unsigned int write_calls_ = 0;
  bool recoverable_error_happened_ = false;
};

// BEGIN (Handle export change): for backward compatibility
class DummySensor : public hardware_interface::SensorInterface
{
  CallbackReturn on_init(
    const hardware_interface::HardwareComponentInterfaceParams & /*params*/) override
  {
    // We hardcode the params
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State & /*previous_state*/) override
  {
    voltage_level_ = 0.0;
    read_calls_ = 0;
    return CallbackReturn::SUCCESS;
  }

  std::vector<hardware_interface::StateInterface> export_state_interfaces() override
  {
    // We can read some voltage level
    std::vector<hardware_interface::StateInterface> state_interfaces;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    state_interfaces.emplace_back(
      hardware_interface::StateInterface("sens1", "voltage", &voltage_level_));
#pragma GCC diagnostic pop
    return state_interfaces;
  }

  hardware_interface::return_type read(
    const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/) override
  {
    ++read_calls_;
    if (read_calls_ == TRIGGER_READ_WRITE_ERROR_CALLS)
    {
      return hardware_interface::return_type::ERROR;
    }

    // no-op, static value
    voltage_level_ = voltage_level_hw_value_;
    return hardware_interface::return_type::OK;
  }

  CallbackReturn on_error(const rclcpp_lifecycle::State & /*previous_state*/) override
  {
    if (!recoverable_error_happened_)
    {
      recoverable_error_happened_ = true;
      return CallbackReturn::SUCCESS;
    }
    else
    {
      return CallbackReturn::ERROR;
    }
    return CallbackReturn::FAILURE;
  }

private:
  double voltage_level_ = std::numeric_limits<double>::quiet_NaN();
  double voltage_level_hw_value_ = 0x666;

  // Helper variables to initiate error on read
  int read_calls_ = 0;
  bool recoverable_error_happened_ = false;
};
// END

class DummySensorDefault : public hardware_interface::SensorInterface
{
  CallbackReturn on_init(
    const hardware_interface::HardwareComponentInterfaceParams & params) override
  {
    if (
      hardware_interface::SensorInterface::on_init(params) !=
      hardware_interface::CallbackReturn::SUCCESS)
    {
      return hardware_interface::CallbackReturn::ERROR;
    }

    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State & /*previous_state*/) override
  {
    set_state("sens1/voltage", 0.0);
    // Should throw as the interface is unknown
    EXPECT_THROW(get_state("joint1/nonexisting/interface"), std::runtime_error);
    EXPECT_THROW(set_state("joint1/nonexisting/interface", 0.0), std::runtime_error);

    read_calls_ = 0;
    return CallbackReturn::SUCCESS;
  }

  hardware_interface::return_type read(
    const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/) override
  {
    ++read_calls_;
    if (read_calls_ == TRIGGER_READ_WRITE_ERROR_CALLS)
    {
      return hardware_interface::return_type::ERROR;
    }

    // no-op, static value
    set_state("sens1/voltage", voltage_level_hw_value_);
    return hardware_interface::return_type::OK;
  }

  CallbackReturn on_error(const rclcpp_lifecycle::State & /*previous_state*/) override
  {
    if (!recoverable_error_happened_)
    {
      recoverable_error_happened_ = true;
      return CallbackReturn::SUCCESS;
    }
    else
    {
      return CallbackReturn::ERROR;
    }
    return CallbackReturn::FAILURE;
  }

private:
  double voltage_level_hw_value_ = 0x666;

  // Helper variables to initiate error on read
  int read_calls_ = 0;
  bool recoverable_error_happened_ = false;
};

class DummySensorJointDefault : public hardware_interface::SensorInterface
{
  CallbackReturn on_init(
    const hardware_interface::HardwareComponentInterfaceParams & params) override
  {
    if (
      hardware_interface::SensorInterface::on_init(params) !=
      hardware_interface::CallbackReturn::SUCCESS)
    {
      return hardware_interface::CallbackReturn::ERROR;
    }

    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State & /*previous_state*/) override
  {
    set_state("joint1/position", 10.0);
    set_state("sens1/voltage", 0.0);
    read_calls_ = 0;
    return CallbackReturn::SUCCESS;
  }

  hardware_interface::return_type read(
    const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/) override
  {
    ++read_calls_;
    if (read_calls_ == TRIGGER_READ_WRITE_ERROR_CALLS)
    {
      return hardware_interface::return_type::ERROR;
    }

    // no-op, static value
    set_state("joint1/position", position_hw_value_);
    set_state("sens1/voltage", voltage_level_hw_value_);
    return hardware_interface::return_type::OK;
  }

  CallbackReturn on_error(const rclcpp_lifecycle::State & /*previous_state*/) override
  {
    if (!recoverable_error_happened_)
    {
      recoverable_error_happened_ = true;
      return CallbackReturn::SUCCESS;
    }
    else
    {
      return CallbackReturn::ERROR;
    }
    return CallbackReturn::FAILURE;
  }

private:
  double position_hw_value_ = 0x777;
  double voltage_level_hw_value_ = 0x666;

  // Helper variables to initiate error on read
  int read_calls_ = 0;
  bool recoverable_error_happened_ = false;
};

// BEGIN (Handle export change): for backward compatibility
class DummySystem : public hardware_interface::SystemInterface
{
  CallbackReturn on_init(
    const hardware_interface::HardwareComponentInterfaceParams & /* params */) override
  {
    // We hardcode the params
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State & /*previous_state*/) override
  {
    for (auto i = 0ul; i < 3; ++i)
    {
      position_state_[i] = 0.0;
      velocity_state_[i] = 0.0;
    }
    // reset command only if error is initiated
    if (recoverable_error_happened_)
    {
      for (auto i = 0ul; i < 3; ++i)
      {
        velocity_command_[i] = 0.0;
      }
    }

    read_calls_ = 0;
    write_calls_ = 0;

    return CallbackReturn::SUCCESS;
  }

  std::vector<hardware_interface::StateInterface> export_state_interfaces() override
  {
    // We can read a position and a velocity
    std::vector<hardware_interface::StateInterface> state_interfaces;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    state_interfaces.emplace_back(
      hardware_interface::StateInterface(
        "joint1", hardware_interface::HW_IF_POSITION, &position_state_[0]));
    state_interfaces.emplace_back(
      hardware_interface::StateInterface(
        "joint1", hardware_interface::HW_IF_VELOCITY, &velocity_state_[0]));
    state_interfaces.emplace_back(
      hardware_interface::StateInterface(
        "joint2", hardware_interface::HW_IF_POSITION, &position_state_[1]));
    state_interfaces.emplace_back(
      hardware_interface::StateInterface(
        "joint2", hardware_interface::HW_IF_VELOCITY, &velocity_state_[1]));
    state_interfaces.emplace_back(
      hardware_interface::StateInterface(
        "joint3", hardware_interface::HW_IF_POSITION, &position_state_[2]));
    state_interfaces.emplace_back(
      hardware_interface::StateInterface(
        "joint3", hardware_interface::HW_IF_VELOCITY, &velocity_state_[2]));
#pragma GCC diagnostic pop
    return state_interfaces;
  }

  std::vector<hardware_interface::CommandInterface> export_command_interfaces() override
  {
    // We can command in velocity
    std::vector<hardware_interface::CommandInterface> command_interfaces;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    command_interfaces.emplace_back(
      hardware_interface::CommandInterface(
        "joint1", hardware_interface::HW_IF_VELOCITY, &velocity_command_[0]));
    command_interfaces.emplace_back(
      hardware_interface::CommandInterface(
        "joint2", hardware_interface::HW_IF_VELOCITY, &velocity_command_[1]));
    command_interfaces.emplace_back(
      hardware_interface::CommandInterface(
        "joint3", hardware_interface::HW_IF_VELOCITY, &velocity_command_[2]));
#pragma GCC diagnostic pop
    return command_interfaces;
  }

  hardware_interface::return_type read(
    const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/) override
  {
    ++read_calls_;
    if (read_calls_ == TRIGGER_READ_WRITE_ERROR_CALLS)
    {
      return hardware_interface::return_type::ERROR;
    }

    // no-op, state is getting propagated within write.
    return hardware_interface::return_type::OK;
  }

  hardware_interface::return_type write(
    const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/) override
  {
    ++write_calls_;
    if (write_calls_ == TRIGGER_READ_WRITE_ERROR_CALLS)
    {
      return hardware_interface::return_type::ERROR;
    }

    for (size_t i = 0; i < 3; ++i)
    {
      position_state_[i] += velocity_command_[0];
      velocity_state_[i] = velocity_command_[0];
    }
    return hardware_interface::return_type::OK;
  }

  CallbackReturn on_shutdown(const rclcpp_lifecycle::State & /*previous_state*/) override
  {
    for (size_t i = 0; i < 3; ++i)
    {
      velocity_state_[i] = 0.0;
    }
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_error(const rclcpp_lifecycle::State & /*previous_state*/) override
  {
    if (!recoverable_error_happened_)
    {
      recoverable_error_happened_ = true;
      return CallbackReturn::SUCCESS;
    }
    else
    {
      return CallbackReturn::ERROR;
    }
    return CallbackReturn::FAILURE;
  }

private:
  std::array<double, 3> position_state_ = {
    {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(),
     std::numeric_limits<double>::quiet_NaN()}};
  std::array<double, 3> velocity_state_ = {
    {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(),
     std::numeric_limits<double>::quiet_NaN()}};
  std::array<double, 3> velocity_command_ = {{0.0, 0.0, 0.0}};

  // Helper variables to initiate error on read
  unsigned int read_calls_ = 0;
  unsigned int write_calls_ = 0;
  bool recoverable_error_happened_ = false;
};
// END

class DummySystemDefault : public hardware_interface::SystemInterface
{
  CallbackReturn on_init(
    const hardware_interface::HardwareComponentInterfaceParams & params) override
  {
    if (
      hardware_interface::SystemInterface::on_init(params) !=
      hardware_interface::CallbackReturn::SUCCESS)
    {
      return hardware_interface::CallbackReturn::ERROR;
    }
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State & /*previous_state*/) override
  {
    for (auto i = 0ul; i < 3; ++i)
    {
      set_state(position_states_[i], 0.0);
      set_state(velocity_states_[i], 0.0);
    }
    // reset command only if error is initiated
    if (recoverable_error_happened_)
    {
      for (auto i = 0ul; i < 3; ++i)
      {
        set_command(velocity_commands_[i], 0.0);
      }
    }
    // Should throw as the interface is unknown
    EXPECT_THROW(get_state("joint1/nonexisting/interface"), std::runtime_error);
    EXPECT_THROW(get_command("joint1/nonexisting/interface"), std::runtime_error);
    EXPECT_THROW(set_state("joint1/nonexisting/interface", 0.0), std::runtime_error);
    EXPECT_THROW(set_command("joint1/nonexisting/interface", 0.0), std::runtime_error);

    read_calls_ = 0;
    write_calls_ = 0;

    return CallbackReturn::SUCCESS;
  }

  hardware_interface::return_type read(
    const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/) override
  {
    ++read_calls_;
    if (read_calls_ == TRIGGER_READ_WRITE_ERROR_CALLS)
    {
      return hardware_interface::return_type::ERROR;
    }

    // no-op, state is getting propagated within write.
    return hardware_interface::return_type::OK;
  }

  hardware_interface::return_type write(
    const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/) override
  {
    ++write_calls_;
    if (write_calls_ == TRIGGER_READ_WRITE_ERROR_CALLS)
    {
      return hardware_interface::return_type::ERROR;
    }

    for (size_t i = 0; i < 3; ++i)
    {
      auto current_pos = get_state(position_states_[i]);
      set_state(position_states_[i], current_pos + get_command(velocity_commands_[i]));
      set_state(velocity_states_[i], get_command(velocity_commands_[i]));
    }
    return hardware_interface::return_type::OK;
  }

  CallbackReturn on_shutdown(const rclcpp_lifecycle::State & /*previous_state*/) override
  {
    for (const auto & velocity_state : velocity_states_)
    {
      set_state(velocity_state, 0.0);
    }
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_error(const rclcpp_lifecycle::State & /*previous_state*/) override
  {
    if (!recoverable_error_happened_)
    {
      recoverable_error_happened_ = true;
      return CallbackReturn::SUCCESS;
    }
    else
    {
      return CallbackReturn::ERROR;
    }
    return CallbackReturn::FAILURE;
  }

private:
  std::vector<std::string> position_states_ = {
    "joint1/position", "joint2/position", "joint3/position"};
  std::vector<std::string> velocity_states_ = {
    "joint1/velocity", "joint2/velocity", "joint3/velocity"};
  std::vector<std::string> velocity_commands_ = {
    "joint1/velocity", "joint2/velocity", "joint3/velocity"};

  // Helper variables to initiate error on read
  unsigned int read_calls_ = 0;
  unsigned int write_calls_ = 0;
  bool recoverable_error_happened_ = false;
};

class DummySystemPreparePerform : public hardware_interface::SystemInterface
{
  // Override the pure virtual functions with default behavior
  CallbackReturn on_init(
    const hardware_interface::HardwareComponentInterfaceParams & /* params */) override
  {
    // We hardcode the params
    return CallbackReturn::SUCCESS;
  }

  hardware_interface::return_type read(
    const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/) override
  {
    return hardware_interface::return_type::OK;
  }

  hardware_interface::return_type write(
    const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/) override
  {
    return hardware_interface::return_type::OK;
  }

  // Custom prepare/perform functions
  hardware_interface::return_type prepare_command_mode_switch(
    const std::vector<std::string> & start_interfaces,
    const std::vector<std::string> & stop_interfaces) override
  {
    // Criteria to test against
    if (start_interfaces.size() != 1)
    {
      return hardware_interface::return_type::ERROR;
    }
    if (stop_interfaces.size() != 2)
    {
      return hardware_interface::return_type::ERROR;
    }
    return hardware_interface::return_type::OK;
  }

  hardware_interface::return_type perform_command_mode_switch(
    const std::vector<std::string> & start_interfaces,
    const std::vector<std::string> & stop_interfaces) override
  {
    // Criteria to test against
    if (start_interfaces.size() != 1)
    {
      return hardware_interface::return_type::ERROR;
    }
    if (stop_interfaces.size() != 2)
    {
      return hardware_interface::return_type::ERROR;
    }
    return hardware_interface::return_type::OK;
  }
};

}  // namespace test_components

// BEGIN (Handle export change): for backward compatibility
TEST(TestComponentInterfaces, dummy_actuator)
{
  hardware_interface::Actuator actuator_hw(std::make_unique<test_components::DummyActuator>());

  hardware_interface::HardwareInfo mock_hw_info{};
  rclcpp::Node::SharedPtr node = std::make_shared<rclcpp::Node>("test_actuator_components");
  hardware_interface::HardwareComponentParams params;
  params.hardware_info = mock_hw_info;
  params.clock = node->get_clock();
  params.logger = node->get_logger();
  auto state = actuator_hw.initialize(params);
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  auto state_interfaces = actuator_hw.export_state_interfaces();
  ASSERT_EQ(2u, state_interfaces.size());
  EXPECT_EQ("joint1/position", state_interfaces[0]->get_name());
  EXPECT_EQ(hardware_interface::HW_IF_POSITION, state_interfaces[0]->get_interface_name());
  EXPECT_EQ("joint1", state_interfaces[0]->get_prefix_name());
  EXPECT_EQ("joint1/velocity", state_interfaces[1]->get_name());
  EXPECT_EQ(hardware_interface::HW_IF_VELOCITY, state_interfaces[1]->get_interface_name());
  EXPECT_EQ("joint1", state_interfaces[1]->get_prefix_name());

  auto command_interfaces = actuator_hw.export_command_interfaces();
  ASSERT_EQ(1u, command_interfaces.size());
  EXPECT_EQ("joint1/velocity", command_interfaces[0]->get_name());
  EXPECT_EQ(hardware_interface::HW_IF_VELOCITY, command_interfaces[0]->get_interface_name());
  EXPECT_EQ("joint1", command_interfaces[0]->get_prefix_name());

  double velocity_value = 1.0;
  ASSERT_TRUE(command_interfaces[0]->set_value(velocity_value));  // velocity
  ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));

  // Noting should change because it is UNCONFIGURED
  for (auto step = 0u; step < 10; ++step)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));

    ASSERT_TRUE(std::isnan(state_interfaces[0]->get_optional().value()));  // position value
    ASSERT_TRUE(std::isnan(state_interfaces[1]->get_optional().value()));  // velocity

    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));
  }

  state = actuator_hw.configure();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::INACTIVE, state.label());

  // Read and Write are working because it is INACTIVE
  for (auto step = 0u; step < 10; ++step)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));

    EXPECT_EQ(
      step * velocity_value, state_interfaces[0]->get_optional().value());  // position value
    EXPECT_EQ(step ? velocity_value : 0, state_interfaces[1]->get_optional().value());  // velocity

    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));
  }

  state = actuator_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  // Read and Write are working because it is ACTIVE
  for (auto step = 0u; step < 10; ++step)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));

    EXPECT_EQ(
      (10 + step) * velocity_value,
      state_interfaces[0]->get_optional().value());                          // position value
    EXPECT_EQ(velocity_value, state_interfaces[1]->get_optional().value());  // velocity

    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));
  }

  state = actuator_hw.shutdown();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());

  // Noting should change because it is FINALIZED
  for (auto step = 0u; step < 10; ++step)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));

    EXPECT_EQ(20 * velocity_value, state_interfaces[0]->get_optional().value());  // position value
    EXPECT_EQ(0, state_interfaces[1]->get_optional().value());                    // velocity

    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));
  }

  EXPECT_EQ(
    hardware_interface::return_type::OK, actuator_hw.prepare_command_mode_switch({""}, {""}));
  EXPECT_EQ(
    hardware_interface::return_type::OK, actuator_hw.perform_command_mode_switch({""}, {""}));
}
// END

TEST(TestComponentInterfaces, dummy_actuator_default)
{
  hardware_interface::Actuator actuator_hw(
    std::make_unique<test_components::DummyActuatorDefault>());
  const std::string urdf_to_test =
    std::string(ros2_control_test_assets::urdf_head) +
    ros2_control_test_assets::valid_urdf_ros2_control_dummy_actuator_only +
    ros2_control_test_assets::urdf_tail;
  const std::vector<hardware_interface::HardwareInfo> control_resources =
    hardware_interface::parse_control_resources_from_urdf(urdf_to_test);
  const hardware_interface::HardwareInfo dummy_actuator = control_resources[0];
  rclcpp::Node::SharedPtr node = std::make_shared<rclcpp::Node>("test_system_components");
  hardware_interface::HardwareComponentParams params;
  params.hardware_info = dummy_actuator;
  params.clock = node->get_clock();
  params.logger = node->get_logger();
  auto state = actuator_hw.initialize(params);

  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  auto state_interfaces = actuator_hw.export_state_interfaces();
  ASSERT_EQ(2u, state_interfaces.size());
  {
    auto [contains, position] =
      test_components::vector_contains(state_interfaces, "joint1/position");
    EXPECT_TRUE(contains);
    EXPECT_EQ("joint1/position", state_interfaces[position]->get_name());
    EXPECT_EQ(hardware_interface::HW_IF_POSITION, state_interfaces[position]->get_interface_name());
    EXPECT_EQ("joint1", state_interfaces[position]->get_prefix_name());
  }
  {
    auto [contains, position] =
      test_components::vector_contains(state_interfaces, "joint1/velocity");
    EXPECT_TRUE(contains);
    EXPECT_EQ("joint1/velocity", state_interfaces[position]->get_name());
    EXPECT_EQ(hardware_interface::HW_IF_VELOCITY, state_interfaces[position]->get_interface_name());
    EXPECT_EQ("joint1", state_interfaces[position]->get_prefix_name());
  }

  auto command_interfaces = actuator_hw.export_command_interfaces();
  ASSERT_EQ(1u, command_interfaces.size());
  {
    auto [contains, position] =
      test_components::vector_contains(command_interfaces, "joint1/velocity");
    EXPECT_TRUE(contains);
    EXPECT_EQ("joint1/velocity", command_interfaces[position]->get_name());
    EXPECT_EQ(
      hardware_interface::HW_IF_VELOCITY, command_interfaces[position]->get_interface_name());
    EXPECT_EQ("joint1", command_interfaces[position]->get_prefix_name());
  }
  double velocity_value = 1.0;
  auto ci_joint1_vel =
    test_components::vector_contains(command_interfaces, "joint1/velocity").second;
  ASSERT_TRUE(command_interfaces[ci_joint1_vel]->set_value(velocity_value));  // velocity
  ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));

  // Noting should change because it is UNCONFIGURED
  auto si_joint1_pos = test_components::vector_contains(state_interfaces, "joint1/position").second;
  auto si_joint1_vel = test_components::vector_contains(state_interfaces, "joint1/velocity").second;
  for (auto step = 0u; step < 10; ++step)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));

    ASSERT_TRUE(
      std::isnan(state_interfaces[si_joint1_pos]->get_optional().value()));  // position value
    ASSERT_TRUE(std::isnan(state_interfaces[si_joint1_vel]->get_optional().value()));  // velocity

    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));
  }

  state = actuator_hw.configure();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::INACTIVE, state.label());

  // Read and Write are working because it is INACTIVE
  for (auto step = 0u; step < 10; ++step)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));

    EXPECT_EQ(
      step * velocity_value,
      state_interfaces[si_joint1_pos]->get_optional().value());  // position value
    EXPECT_EQ(
      step ? velocity_value : 0,
      state_interfaces[si_joint1_vel]->get_optional().value());  // velocity

    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));
  }

  state = actuator_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  // Read and Write are working because it is ACTIVE
  for (auto step = 0u; step < 10; ++step)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));

    EXPECT_EQ(
      (10 + step) * velocity_value,
      state_interfaces[si_joint1_pos]->get_optional().value());  // position value
    EXPECT_EQ(velocity_value, state_interfaces[si_joint1_vel]->get_optional().value());  // velocity

    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));
  }

  state = actuator_hw.shutdown();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());

  // Noting should change because it is FINALIZED
  for (auto step = 0u; step < 10; ++step)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));

    EXPECT_EQ(
      20 * velocity_value,
      state_interfaces[si_joint1_pos]->get_optional().value());             // position value
    EXPECT_EQ(0, state_interfaces[si_joint1_vel]->get_optional().value());  // velocity

    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));
  }

  EXPECT_EQ(
    hardware_interface::return_type::OK, actuator_hw.prepare_command_mode_switch({""}, {""}));
  EXPECT_EQ(
    hardware_interface::return_type::OK, actuator_hw.perform_command_mode_switch({""}, {""}));
}

// BEGIN (Handle export change): for backward compatibility
TEST(TestComponentInterfaces, dummy_sensor)
{
  hardware_interface::Sensor sensor_hw(std::make_unique<test_components::DummySensor>());

  hardware_interface::HardwareInfo mock_hw_info{};
  rclcpp::Node::SharedPtr node = std::make_shared<rclcpp::Node>("test_sensor_components");
  hardware_interface::HardwareComponentParams params;
  params.hardware_info = mock_hw_info;
  params.clock = node->get_clock();
  params.logger = node->get_logger();
  auto state = sensor_hw.initialize(params);
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  auto state_interfaces = sensor_hw.export_state_interfaces();
  ASSERT_EQ(1u, state_interfaces.size());
  EXPECT_EQ("sens1/voltage", state_interfaces[0]->get_name());
  EXPECT_EQ("voltage", state_interfaces[0]->get_interface_name());
  EXPECT_EQ("sens1", state_interfaces[0]->get_prefix_name());
  EXPECT_TRUE(std::isnan(state_interfaces[0]->get_optional().value()));

  // Not updated because is is UNCONFIGURED
  sensor_hw.read(TIME, PERIOD);
  EXPECT_TRUE(std::isnan(state_interfaces[0]->get_optional().value()));

  // Updated because is is INACTIVE
  state = sensor_hw.configure();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::INACTIVE, state.label());
  EXPECT_EQ(0.0, state_interfaces[0]->get_optional().value());

  // It can read now
  sensor_hw.read(TIME, PERIOD);
  EXPECT_EQ(0x666, state_interfaces[0]->get_optional().value());
}
// END

TEST(TestComponentInterfaces, dummy_sensor_default)
{
  hardware_interface::Sensor sensor_hw(std::make_unique<test_components::DummySensorDefault>());

  const std::string urdf_to_test =
    std::string(ros2_control_test_assets::urdf_head) +
    ros2_control_test_assets::valid_urdf_ros2_control_voltage_sensor_only +
    ros2_control_test_assets::urdf_tail;
  const std::vector<hardware_interface::HardwareInfo> control_resources =
    hardware_interface::parse_control_resources_from_urdf(urdf_to_test);
  const hardware_interface::HardwareInfo voltage_sensor_res = control_resources[0];
  rclcpp::Node::SharedPtr node = std::make_shared<rclcpp::Node>("test_system_components");
  hardware_interface::HardwareComponentParams params;
  params.hardware_info = voltage_sensor_res;
  params.clock = node->get_clock();
  params.logger = node->get_logger();
  auto state = sensor_hw.initialize(params);
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  auto state_interfaces = sensor_hw.export_state_interfaces();
  ASSERT_EQ(1u, state_interfaces.size());
  {
    auto [contains, position] = test_components::vector_contains(state_interfaces, "sens1/voltage");
    EXPECT_TRUE(contains);
    EXPECT_EQ("sens1/voltage", state_interfaces[position]->get_name());
    EXPECT_EQ("voltage", state_interfaces[position]->get_interface_name());
    EXPECT_EQ("sens1", state_interfaces[position]->get_prefix_name());
    EXPECT_TRUE(std::isnan(state_interfaces[position]->get_optional().value()));
  }

  // Not updated because is is UNCONFIGURED
  auto si_sens1_vol = test_components::vector_contains(state_interfaces, "sens1/voltage").second;
  sensor_hw.read(TIME, PERIOD);
  EXPECT_TRUE(std::isnan(state_interfaces[si_sens1_vol]->get_optional().value()));

  // Updated because is is INACTIVE
  state = sensor_hw.configure();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::INACTIVE, state.label());
  EXPECT_EQ(0.0, state_interfaces[si_sens1_vol]->get_optional().value());

  // It can read now
  sensor_hw.read(TIME, PERIOD);
  EXPECT_EQ(0x666, state_interfaces[si_sens1_vol]->get_optional().value());
}

TEST(TestComponentInterfaces, dummy_sensor_default_joint)
{
  hardware_interface::Sensor sensor_hw(
    std::make_unique<test_components::DummySensorJointDefault>());

  const std::string urdf_to_test =
    std::string(ros2_control_test_assets::urdf_head) +
    ros2_control_test_assets::valid_urdf_ros2_control_joint_voltage_sensor +
    ros2_control_test_assets::urdf_tail;
  const std::vector<hardware_interface::HardwareInfo> control_resources =
    hardware_interface::parse_control_resources_from_urdf(urdf_to_test);
  const hardware_interface::HardwareInfo sensor_res = control_resources[0];
  rclcpp::Node::SharedPtr node = std::make_shared<rclcpp::Node>("test_system_components");
  hardware_interface::HardwareComponentParams params;
  params.hardware_info = sensor_res;
  params.clock = node->get_clock();
  params.logger = node->get_logger();
  auto state = sensor_hw.initialize(params);
  ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  ASSERT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  auto state_interfaces = sensor_hw.export_state_interfaces();
  ASSERT_EQ(2u, state_interfaces.size());

  auto [contains_sens1_vol, si_sens1_vol] =
    test_components::vector_contains(state_interfaces, "sens1/voltage");
  ASSERT_TRUE(contains_sens1_vol);
  EXPECT_EQ("sens1/voltage", state_interfaces[si_sens1_vol]->get_name());
  EXPECT_EQ("voltage", state_interfaces[si_sens1_vol]->get_interface_name());
  EXPECT_EQ("sens1", state_interfaces[si_sens1_vol]->get_prefix_name());
  EXPECT_TRUE(std::isnan(state_interfaces[si_sens1_vol]->get_optional().value()));

  auto [contains_joint1_pos, si_joint1_pos] =
    test_components::vector_contains(state_interfaces, "joint1/position");
  ASSERT_TRUE(contains_joint1_pos);
  EXPECT_EQ("joint1/position", state_interfaces[si_joint1_pos]->get_name());
  EXPECT_EQ("position", state_interfaces[si_joint1_pos]->get_interface_name());
  EXPECT_EQ("joint1", state_interfaces[si_joint1_pos]->get_prefix_name());
  EXPECT_TRUE(std::isnan(state_interfaces[si_joint1_pos]->get_optional().value()));

  // Not updated because is is UNCONFIGURED
  sensor_hw.read(TIME, PERIOD);
  EXPECT_TRUE(std::isnan(state_interfaces[si_sens1_vol]->get_optional().value()));
  EXPECT_TRUE(std::isnan(state_interfaces[si_joint1_pos]->get_optional().value()));

  // Updated because is is INACTIVE
  state = sensor_hw.configure();
  ASSERT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, state.id());
  ASSERT_EQ(hardware_interface::lifecycle_state_names::INACTIVE, state.label());
  EXPECT_EQ(0.0, state_interfaces[si_sens1_vol]->get_optional().value());
  EXPECT_EQ(10.0, state_interfaces[si_joint1_pos]->get_optional().value());

  // It can read now
  sensor_hw.read(TIME, PERIOD);
  EXPECT_EQ(0x666, state_interfaces[si_sens1_vol]->get_optional().value());
  EXPECT_EQ(0x777, state_interfaces[si_joint1_pos]->get_optional().value());
}

// BEGIN (Handle export change): for backward compatibility
TEST(TestComponentInterfaces, dummy_system)
{
  hardware_interface::System system_hw(std::make_unique<test_components::DummySystem>());

  hardware_interface::HardwareInfo mock_hw_info{};
  rclcpp::Node::SharedPtr node = std::make_shared<rclcpp::Node>("test_system_components");
  hardware_interface::HardwareComponentParams params;
  params.hardware_info = mock_hw_info;
  params.clock = node->get_clock();
  params.logger = node->get_logger();
  auto state = system_hw.initialize(params);
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  auto state_interfaces = system_hw.export_state_interfaces();
  ASSERT_EQ(6u, state_interfaces.size());
  EXPECT_EQ("joint1/position", state_interfaces[0]->get_name());
  EXPECT_EQ(hardware_interface::HW_IF_POSITION, state_interfaces[0]->get_interface_name());
  EXPECT_EQ("joint1", state_interfaces[0]->get_prefix_name());
  EXPECT_EQ("joint1/velocity", state_interfaces[1]->get_name());
  EXPECT_EQ(hardware_interface::HW_IF_VELOCITY, state_interfaces[1]->get_interface_name());
  EXPECT_EQ("joint1", state_interfaces[1]->get_prefix_name());
  EXPECT_EQ("joint2/position", state_interfaces[2]->get_name());
  EXPECT_EQ(hardware_interface::HW_IF_POSITION, state_interfaces[2]->get_interface_name());
  EXPECT_EQ("joint2", state_interfaces[2]->get_prefix_name());
  EXPECT_EQ("joint2/velocity", state_interfaces[3]->get_name());
  EXPECT_EQ(hardware_interface::HW_IF_VELOCITY, state_interfaces[3]->get_interface_name());
  EXPECT_EQ("joint2", state_interfaces[3]->get_prefix_name());
  EXPECT_EQ("joint3/position", state_interfaces[4]->get_name());
  EXPECT_EQ(hardware_interface::HW_IF_POSITION, state_interfaces[4]->get_interface_name());
  EXPECT_EQ("joint3", state_interfaces[4]->get_prefix_name());
  EXPECT_EQ("joint3/velocity", state_interfaces[5]->get_name());
  EXPECT_EQ(hardware_interface::HW_IF_VELOCITY, state_interfaces[5]->get_interface_name());
  EXPECT_EQ("joint3", state_interfaces[5]->get_prefix_name());

  auto command_interfaces = system_hw.export_command_interfaces();
  ASSERT_EQ(3u, command_interfaces.size());
  EXPECT_EQ("joint1/velocity", command_interfaces[0]->get_name());
  EXPECT_EQ(hardware_interface::HW_IF_VELOCITY, command_interfaces[0]->get_interface_name());
  EXPECT_EQ("joint1", command_interfaces[0]->get_prefix_name());
  EXPECT_EQ("joint2/velocity", command_interfaces[1]->get_name());
  EXPECT_EQ(hardware_interface::HW_IF_VELOCITY, command_interfaces[1]->get_interface_name());
  EXPECT_EQ("joint2", command_interfaces[1]->get_prefix_name());
  EXPECT_EQ("joint3/velocity", command_interfaces[2]->get_name());
  EXPECT_EQ(hardware_interface::HW_IF_VELOCITY, command_interfaces[2]->get_interface_name());
  EXPECT_EQ("joint3", command_interfaces[2]->get_prefix_name());

  double velocity_value = 1.0;
  ASSERT_TRUE(command_interfaces[0]->set_value(velocity_value));  // velocity
  ASSERT_TRUE(command_interfaces[1]->set_value(velocity_value));  // velocity
  ASSERT_TRUE(command_interfaces[2]->set_value(velocity_value));  // velocity
  ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));

  // Noting should change because it is UNCONFIGURED
  for (auto step = 0u; step < 10; ++step)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));

    ASSERT_TRUE(std::isnan(state_interfaces[0]->get_optional().value()));  // position value
    ASSERT_TRUE(std::isnan(state_interfaces[1]->get_optional().value()));  // velocity
    ASSERT_TRUE(std::isnan(state_interfaces[2]->get_optional().value()));  // position value
    ASSERT_TRUE(std::isnan(state_interfaces[3]->get_optional().value()));  // velocity
    ASSERT_TRUE(std::isnan(state_interfaces[4]->get_optional().value()));  // position value
    ASSERT_TRUE(std::isnan(state_interfaces[5]->get_optional().value()));  // velocity

    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));
  }

  state = system_hw.configure();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::INACTIVE, state.label());

  // Values should 0 because only read should work when INACTIVE
  for (auto step = 0u; step < 10; ++step)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));

    EXPECT_EQ(0, state_interfaces[0]->get_optional().value());  // position value
    EXPECT_EQ(0, state_interfaces[1]->get_optional().value());  // velocity
    EXPECT_EQ(0, state_interfaces[2]->get_optional().value());  // position value
    EXPECT_EQ(0, state_interfaces[3]->get_optional().value());  // velocity
    EXPECT_EQ(0, state_interfaces[4]->get_optional().value());  // position value
    EXPECT_EQ(0, state_interfaces[5]->get_optional().value());  // velocity

    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));
  }

  state = system_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  // Read and Write are working because it is ACTIVE
  for (auto step = 0u; step < 10; ++step)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));

    EXPECT_EQ(
      step * velocity_value, state_interfaces[0]->get_optional().value());  // position value
    EXPECT_EQ(step ? velocity_value : 0, state_interfaces[1]->get_optional().value());  // velocity
    EXPECT_EQ(
      step * velocity_value, state_interfaces[2]->get_optional().value());  // position value
    EXPECT_EQ(step ? velocity_value : 0, state_interfaces[3]->get_optional().value());  // velocity
    EXPECT_EQ(
      step * velocity_value, state_interfaces[4]->get_optional().value());  // position value
    EXPECT_EQ(step ? velocity_value : 0, state_interfaces[5]->get_optional().value());  // velocity

    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));
  }

  state = system_hw.shutdown();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());

  // Noting should change because it is FINALIZED
  for (auto step = 0u; step < 10; ++step)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));

    EXPECT_EQ(10 * velocity_value, state_interfaces[0]->get_optional().value());  // position value
    EXPECT_EQ(0.0, state_interfaces[1]->get_optional().value());                  // velocity
    EXPECT_EQ(10 * velocity_value, state_interfaces[2]->get_optional().value());  // position value
    EXPECT_EQ(0.0, state_interfaces[3]->get_optional().value());                  // velocity
    EXPECT_EQ(10 * velocity_value, state_interfaces[4]->get_optional().value());  // position value
    EXPECT_EQ(0.0, state_interfaces[5]->get_optional().value());                  // velocity

    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));
  }

  EXPECT_EQ(hardware_interface::return_type::OK, system_hw.prepare_command_mode_switch({}, {}));
  EXPECT_EQ(hardware_interface::return_type::OK, system_hw.perform_command_mode_switch({}, {}));
}
// END

TEST(TestComponentInterfaces, dummy_system_default)
{
  hardware_interface::System system_hw(std::make_unique<test_components::DummySystemDefault>());

  const std::string urdf_to_test =
    std::string(ros2_control_test_assets::urdf_head) +
    ros2_control_test_assets::valid_urdf_ros2_control_dummy_system_robot +
    ros2_control_test_assets::urdf_tail;
  const std::vector<hardware_interface::HardwareInfo> control_resources =
    hardware_interface::parse_control_resources_from_urdf(urdf_to_test);
  const hardware_interface::HardwareInfo dummy_system = control_resources[0];
  rclcpp::Node::SharedPtr node = std::make_shared<rclcpp::Node>("test_system_components");
  hardware_interface::HardwareComponentParams params;
  params.hardware_info = dummy_system;
  params.clock = node->get_clock();
  params.logger = node->get_logger();
  auto state = system_hw.initialize(params);
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  auto state_interfaces = system_hw.export_state_interfaces();
  ASSERT_EQ(6u, state_interfaces.size());
  {
    auto [contains, position] =
      test_components::vector_contains(state_interfaces, "joint1/position");
    EXPECT_TRUE(contains);
    EXPECT_EQ("joint1/position", state_interfaces[position]->get_name());
    EXPECT_EQ(hardware_interface::HW_IF_POSITION, state_interfaces[position]->get_interface_name());
    EXPECT_EQ("joint1", state_interfaces[position]->get_prefix_name());
  }
  {
    auto [contains, position] =
      test_components::vector_contains(state_interfaces, "joint1/velocity");
    EXPECT_TRUE(contains);
    EXPECT_EQ("joint1/velocity", state_interfaces[position]->get_name());
    EXPECT_EQ(hardware_interface::HW_IF_VELOCITY, state_interfaces[position]->get_interface_name());
    EXPECT_EQ("joint1", state_interfaces[position]->get_prefix_name());
  }
  {
    auto [contains, position] =
      test_components::vector_contains(state_interfaces, "joint2/position");
    EXPECT_TRUE(contains);
    EXPECT_EQ("joint2/position", state_interfaces[position]->get_name());
    EXPECT_EQ(hardware_interface::HW_IF_POSITION, state_interfaces[position]->get_interface_name());
    EXPECT_EQ("joint2", state_interfaces[position]->get_prefix_name());
  }
  {
    auto [contains, position] =
      test_components::vector_contains(state_interfaces, "joint2/velocity");
    EXPECT_TRUE(contains);
    EXPECT_EQ("joint2/velocity", state_interfaces[position]->get_name());
    EXPECT_EQ(hardware_interface::HW_IF_VELOCITY, state_interfaces[position]->get_interface_name());
    EXPECT_EQ("joint2", state_interfaces[position]->get_prefix_name());
  }
  {
    auto [contains, position] =
      test_components::vector_contains(state_interfaces, "joint3/position");
    EXPECT_TRUE(contains);
    EXPECT_EQ("joint3/position", state_interfaces[position]->get_name());
    EXPECT_EQ(hardware_interface::HW_IF_POSITION, state_interfaces[position]->get_interface_name());
    EXPECT_EQ("joint3", state_interfaces[position]->get_prefix_name());
  }
  {
    auto [contains, position] =
      test_components::vector_contains(state_interfaces, "joint3/velocity");
    EXPECT_TRUE(contains);
    EXPECT_EQ("joint3/velocity", state_interfaces[position]->get_name());
    EXPECT_EQ(hardware_interface::HW_IF_VELOCITY, state_interfaces[position]->get_interface_name());
    EXPECT_EQ("joint3", state_interfaces[position]->get_prefix_name());
  }

  auto command_interfaces = system_hw.export_command_interfaces();
  ASSERT_EQ(3u, command_interfaces.size());
  {
    auto [contains, position] =
      test_components::vector_contains(command_interfaces, "joint1/velocity");
    EXPECT_TRUE(contains);
    EXPECT_EQ("joint1/velocity", command_interfaces[position]->get_name());
    EXPECT_EQ(
      hardware_interface::HW_IF_VELOCITY, command_interfaces[position]->get_interface_name());
    EXPECT_EQ("joint1", command_interfaces[position]->get_prefix_name());
  }
  {
    auto [contains, position] =
      test_components::vector_contains(command_interfaces, "joint2/velocity");
    EXPECT_TRUE(contains);
    EXPECT_EQ("joint2/velocity", command_interfaces[position]->get_name());
    EXPECT_EQ(
      hardware_interface::HW_IF_VELOCITY, command_interfaces[position]->get_interface_name());
    EXPECT_EQ("joint2", command_interfaces[position]->get_prefix_name());
  }
  {
    auto [contains, position] =
      test_components::vector_contains(command_interfaces, "joint3/velocity");
    EXPECT_TRUE(contains);
    EXPECT_EQ("joint3/velocity", command_interfaces[position]->get_name());
    EXPECT_EQ(
      hardware_interface::HW_IF_VELOCITY, command_interfaces[position]->get_interface_name());
    EXPECT_EQ("joint3", command_interfaces[position]->get_prefix_name());
  }

  auto ci_joint1_vel =
    test_components::vector_contains(command_interfaces, "joint1/velocity").second;
  auto ci_joint2_vel =
    test_components::vector_contains(command_interfaces, "joint2/velocity").second;
  auto ci_joint3_vel =
    test_components::vector_contains(command_interfaces, "joint3/velocity").second;
  double velocity_value = 1.0;
  ASSERT_TRUE(command_interfaces[ci_joint1_vel]->set_value(velocity_value));  // velocity
  ASSERT_TRUE(command_interfaces[ci_joint2_vel]->set_value(velocity_value));  // velocity
  ASSERT_TRUE(command_interfaces[ci_joint3_vel]->set_value(velocity_value));  // velocity
  ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));

  // Noting should change because it is UNCONFIGURED
  auto si_joint1_pos = test_components::vector_contains(state_interfaces, "joint1/position").second;
  auto si_joint1_vel = test_components::vector_contains(state_interfaces, "joint1/velocity").second;
  auto si_joint2_pos = test_components::vector_contains(state_interfaces, "joint2/position").second;
  auto si_joint2_vel = test_components::vector_contains(state_interfaces, "joint2/velocity").second;
  auto si_joint3_pos = test_components::vector_contains(state_interfaces, "joint3/position").second;
  auto si_joint3_vel = test_components::vector_contains(state_interfaces, "joint3/velocity").second;
  for (auto step = 0u; step < 10; ++step)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));

    ASSERT_TRUE(
      std::isnan(state_interfaces[si_joint1_pos]->get_optional().value()));  // position value
    ASSERT_TRUE(std::isnan(state_interfaces[si_joint1_vel]->get_optional().value()));  // velocity
    ASSERT_TRUE(
      std::isnan(state_interfaces[si_joint2_pos]->get_optional().value()));  // position value
    ASSERT_TRUE(std::isnan(state_interfaces[si_joint2_vel]->get_optional().value()));  // velocity
    ASSERT_TRUE(
      std::isnan(state_interfaces[si_joint3_pos]->get_optional().value()));  // position value
    ASSERT_TRUE(std::isnan(state_interfaces[si_joint3_vel]->get_optional().value()));  // velocity

    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));
  }

  state = system_hw.configure();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::INACTIVE, state.label());

  // Values should 0 because only read should work when INACTIVE
  for (auto step = 0u; step < 10; ++step)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));

    EXPECT_EQ(0, state_interfaces[si_joint1_pos]->get_optional().value());  // position value
    EXPECT_EQ(0, state_interfaces[si_joint1_vel]->get_optional().value());  // velocity
    EXPECT_EQ(0, state_interfaces[si_joint2_pos]->get_optional().value());  // position value
    EXPECT_EQ(0, state_interfaces[si_joint2_vel]->get_optional().value());  // velocity
    EXPECT_EQ(0, state_interfaces[si_joint3_pos]->get_optional().value());  // position value
    EXPECT_EQ(0, state_interfaces[si_joint3_vel]->get_optional().value());  // velocity

    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));
  }

  state = system_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  // Read and Write are working because it is ACTIVE
  for (auto step = 0u; step < 10; ++step)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));

    EXPECT_EQ(
      step * velocity_value,
      state_interfaces[si_joint1_pos]->get_optional().value());  // position value
    EXPECT_EQ(
      step ? velocity_value : 0,
      state_interfaces[si_joint1_vel]->get_optional().value());  // velocity
    EXPECT_EQ(
      step * velocity_value,
      state_interfaces[si_joint2_pos]->get_optional().value());  // position value
    EXPECT_EQ(
      step ? velocity_value : 0,
      state_interfaces[si_joint2_vel]->get_optional().value());  // velocity
    EXPECT_EQ(
      step * velocity_value,
      state_interfaces[si_joint3_pos]->get_optional().value());  // position value
    EXPECT_EQ(
      step ? velocity_value : 0,
      state_interfaces[si_joint3_vel]->get_optional().value());  // velocity

    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));
  }

  state = system_hw.shutdown();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());

  // Noting should change because it is FINALIZED
  for (auto step = 0u; step < 10; ++step)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));

    EXPECT_EQ(
      10 * velocity_value,
      state_interfaces[si_joint1_pos]->get_optional().value());               // position value
    EXPECT_EQ(0.0, state_interfaces[si_joint1_vel]->get_optional().value());  // velocity
    EXPECT_EQ(
      10 * velocity_value,
      state_interfaces[si_joint2_pos]->get_optional().value());               // position value
    EXPECT_EQ(0.0, state_interfaces[si_joint2_vel]->get_optional().value());  // velocity
    EXPECT_EQ(
      10 * velocity_value,
      state_interfaces[si_joint3_pos]->get_optional().value());               // position value
    EXPECT_EQ(0.0, state_interfaces[si_joint3_vel]->get_optional().value());  // velocity

    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));
  }

  EXPECT_EQ(hardware_interface::return_type::OK, system_hw.prepare_command_mode_switch({}, {}));
  EXPECT_EQ(hardware_interface::return_type::OK, system_hw.perform_command_mode_switch({}, {}));
}

TEST(TestComponentInterfaces, dummy_command_mode_system)
{
  hardware_interface::System system_hw(
    std::make_unique<test_components::DummySystemPreparePerform>());
  hardware_interface::HardwareInfo mock_hw_info{};
  rclcpp::Node::SharedPtr node = std::make_shared<rclcpp::Node>("test_system_components");
  hardware_interface::HardwareComponentParams params;
  params.hardware_info = mock_hw_info;
  params.clock = node->get_clock();
  params.logger = node->get_logger();
  auto state = system_hw.initialize(params);
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  std::vector<std::string> one_key = {"joint1/position"};
  std::vector<std::string> two_keys = {"joint1/position", "joint1/velocity"};

  // Only calls with (one_key, two_keys) should return OK
  EXPECT_EQ(
    hardware_interface::return_type::ERROR,
    system_hw.prepare_command_mode_switch(one_key, one_key));
  EXPECT_EQ(
    hardware_interface::return_type::ERROR,
    system_hw.perform_command_mode_switch(one_key, one_key));
  EXPECT_EQ(
    hardware_interface::return_type::OK, system_hw.prepare_command_mode_switch(one_key, two_keys));
  EXPECT_EQ(
    hardware_interface::return_type::OK, system_hw.perform_command_mode_switch(one_key, two_keys));
  EXPECT_EQ(
    hardware_interface::return_type::ERROR,
    system_hw.prepare_command_mode_switch(two_keys, one_key));
  EXPECT_EQ(
    hardware_interface::return_type::ERROR,
    system_hw.perform_command_mode_switch(two_keys, one_key));
}

// BEGIN (Handle export change): for backward compatibility
TEST(TestComponentInterfaces, dummy_actuator_read_error_behavior)
{
  hardware_interface::Actuator actuator_hw(std::make_unique<test_components::DummyActuator>());

  hardware_interface::HardwareInfo mock_hw_info{};
  rclcpp::Node::SharedPtr node = std::make_shared<rclcpp::Node>("test_actuator_components");
  hardware_interface::HardwareComponentParams params;
  params.hardware_info = mock_hw_info;
  params.clock = node->get_clock();
  params.logger = node->get_logger();
  auto state = actuator_hw.initialize(params);
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  auto state_interfaces = actuator_hw.export_state_interfaces();
  auto command_interfaces = actuator_hw.export_command_interfaces();
  state = actuator_hw.configure();
  state = actuator_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));
  ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));

  // Initiate error on write (this is first time therefore recoverable)
  for (auto i = 2ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, actuator_hw.read(TIME, PERIOD));

  state = actuator_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  // activate again and expect reset values
  state = actuator_hw.configure();
  EXPECT_EQ(state_interfaces[0]->get_optional().value(), 0.0);
  EXPECT_EQ(command_interfaces[0]->get_optional().value(), 0.0);

  state = actuator_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));
  ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));

  // Initiate error on write (this is the second time therefore unrecoverable)
  for (auto i = 2ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, actuator_hw.read(TIME, PERIOD));

  state = actuator_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());

  // can not change state anymore
  state = actuator_hw.configure();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());
}
// END

TEST(TestComponentInterfaces, dummy_actuator_default_read_error_behavior)
{
  hardware_interface::Actuator actuator_hw(
    std::make_unique<test_components::DummyActuatorDefault>());

  const std::string urdf_to_test =
    std::string(ros2_control_test_assets::urdf_head) +
    ros2_control_test_assets::valid_urdf_ros2_control_dummy_actuator_only +
    ros2_control_test_assets::urdf_tail;
  const std::vector<hardware_interface::HardwareInfo> control_resources =
    hardware_interface::parse_control_resources_from_urdf(urdf_to_test);
  const hardware_interface::HardwareInfo dummy_actuator = control_resources[0];
  rclcpp::Node::SharedPtr node = std::make_shared<rclcpp::Node>("test_system_components");
  hardware_interface::HardwareComponentParams params;
  params.hardware_info = dummy_actuator;
  params.clock = node->get_clock();
  params.logger = node->get_logger();
  auto state = actuator_hw.initialize(params);

  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  auto state_interfaces = actuator_hw.export_state_interfaces();
  auto command_interfaces = actuator_hw.export_command_interfaces();
  state = actuator_hw.configure();
  state = actuator_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));
  ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));

  // Initiate error on write (this is first time therefore recoverable)
  for (auto i = 2ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, actuator_hw.read(TIME, PERIOD));

  state = actuator_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  // activate again and expect reset values
  auto si_joint1_pos = test_components::vector_contains(state_interfaces, "joint1/position").second;
  auto ci_joint1_vel =
    test_components::vector_contains(command_interfaces, "joint1/velocity").second;
  state = actuator_hw.configure();
  EXPECT_EQ(state_interfaces[si_joint1_pos]->get_optional().value(), 0.0);
  EXPECT_EQ(command_interfaces[ci_joint1_vel]->get_optional().value(), 0.0);

  state = actuator_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));
  ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));

  // Initiate error on write (this is the second time therefore unrecoverable)
  for (auto i = 2ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, actuator_hw.read(TIME, PERIOD));

  state = actuator_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());

  // can not change state anymore
  state = actuator_hw.configure();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());
}

// BEGIN (Handle export change): for backward compatibility
TEST(TestComponentInterfaces, dummy_actuator_write_error_behavior)
{
  hardware_interface::Actuator actuator_hw(std::make_unique<test_components::DummyActuator>());

  hardware_interface::HardwareInfo mock_hw_info{};
  rclcpp::Node::SharedPtr node = std::make_shared<rclcpp::Node>("test_actuator_components");
  hardware_interface::HardwareComponentParams params;
  params.hardware_info = mock_hw_info;
  params.clock = node->get_clock();
  params.logger = node->get_logger();
  auto state = actuator_hw.initialize(params);
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  auto state_interfaces = actuator_hw.export_state_interfaces();
  auto command_interfaces = actuator_hw.export_command_interfaces();
  state = actuator_hw.configure();
  state = actuator_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));
  ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));

  // Initiate error on write (this is first time therefore recoverable)
  for (auto i = 2ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, actuator_hw.write(TIME, PERIOD));

  state = actuator_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  // activate again and expect reset values
  state = actuator_hw.configure();
  EXPECT_EQ(state_interfaces[0]->get_optional().value(), 0.0);
  EXPECT_EQ(command_interfaces[0]->get_optional().value(), 0.0);

  state = actuator_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));
  ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));

  // Initiate error on write (this is the second time therefore unrecoverable)
  for (auto i = 2ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, actuator_hw.write(TIME, PERIOD));

  state = actuator_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());

  // can not change state anymore
  state = actuator_hw.configure();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());
}
// END

TEST(TestComponentInterfaces, dummy_actuator_default_write_error_behavior)
{
  hardware_interface::Actuator actuator_hw(
    std::make_unique<test_components::DummyActuatorDefault>());

  const std::string urdf_to_test =
    std::string(ros2_control_test_assets::urdf_head) +
    ros2_control_test_assets::valid_urdf_ros2_control_dummy_actuator_only +
    ros2_control_test_assets::urdf_tail;
  const std::vector<hardware_interface::HardwareInfo> control_resources =
    hardware_interface::parse_control_resources_from_urdf(urdf_to_test);
  const hardware_interface::HardwareInfo dummy_actuator = control_resources[0];
  rclcpp::Node::SharedPtr node = std::make_shared<rclcpp::Node>("test_system_components");
  hardware_interface::HardwareComponentParams params;
  params.hardware_info = dummy_actuator;
  params.clock = node->get_clock();
  params.logger = node->get_logger();
  auto state = actuator_hw.initialize(params);
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  auto state_interfaces = actuator_hw.export_state_interfaces();
  auto command_interfaces = actuator_hw.export_command_interfaces();
  state = actuator_hw.configure();
  state = actuator_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));
  ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));

  // Initiate error on write (this is first time therefore recoverable)
  for (auto i = 2ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, actuator_hw.write(TIME, PERIOD));

  state = actuator_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  // activate again and expect reset values
  auto si_joint1_pos = test_components::vector_contains(state_interfaces, "joint1/position").second;
  auto ci_joint1_vel =
    test_components::vector_contains(command_interfaces, "joint1/velocity").second;
  state = actuator_hw.configure();
  EXPECT_EQ(state_interfaces[si_joint1_pos]->get_optional().value(), 0.0);
  EXPECT_EQ(command_interfaces[ci_joint1_vel]->get_optional().value(), 0.0);

  state = actuator_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.read(TIME, PERIOD));
  ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));

  // Initiate error on write (this is the second time therefore unrecoverable)
  for (auto i = 2ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, actuator_hw.write(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, actuator_hw.write(TIME, PERIOD));

  state = actuator_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());

  // can not change state anymore
  state = actuator_hw.configure();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());
}

// BEGIN (Handle export change): for backward compatibility
TEST(TestComponentInterfaces, dummy_sensor_read_error_behavior)
{
  hardware_interface::Sensor sensor_hw(std::make_unique<test_components::DummySensor>());

  hardware_interface::HardwareInfo mock_hw_info{};
  rclcpp::Node::SharedPtr node = std::make_shared<rclcpp::Node>("test_sensor_components");
  hardware_interface::HardwareComponentParams params;
  params.hardware_info = mock_hw_info;
  params.clock = node->get_clock();
  params.logger = node->get_logger();
  auto state = sensor_hw.initialize(params);

  auto state_interfaces = sensor_hw.export_state_interfaces();
  // Updated because is is INACTIVE
  state = sensor_hw.configure();
  state = sensor_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  ASSERT_EQ(hardware_interface::return_type::OK, sensor_hw.read(TIME, PERIOD));

  // Initiate recoverable error - call read 99 times OK and on 100-time will return error
  for (auto i = 2ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, sensor_hw.read(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, sensor_hw.read(TIME, PERIOD));

  state = sensor_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  // Noting should change because it is UNCONFIGURED
  for (auto step = 0u; step < 10; ++step)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, sensor_hw.read(TIME, PERIOD));
  }

  // activate again and expect reset values
  state = sensor_hw.configure();
  EXPECT_EQ(state_interfaces[0]->get_optional().value(), 0.0);

  state = sensor_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  // Initiate unrecoverable error - call read 99 times OK and on 100-time will return error
  for (auto i = 1ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, sensor_hw.read(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, sensor_hw.read(TIME, PERIOD));

  state = sensor_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());

  // Noting should change because it is FINALIZED
  for (auto step = 0u; step < 10; ++step)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, sensor_hw.read(TIME, PERIOD));
  }

  // can not change state anymore
  state = sensor_hw.configure();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());
}
// END

TEST(TestComponentInterfaces, dummy_sensor_default_read_error_behavior)
{
  hardware_interface::Sensor sensor_hw(std::make_unique<test_components::DummySensorDefault>());

  const std::string urdf_to_test =
    std::string(ros2_control_test_assets::urdf_head) +
    ros2_control_test_assets::valid_urdf_ros2_control_voltage_sensor_only +
    ros2_control_test_assets::urdf_tail;
  const std::vector<hardware_interface::HardwareInfo> control_resources =
    hardware_interface::parse_control_resources_from_urdf(urdf_to_test);
  const hardware_interface::HardwareInfo voltage_sensor_res = control_resources[0];
  rclcpp::Node::SharedPtr node = std::make_shared<rclcpp::Node>("test_system_components");
  hardware_interface::HardwareComponentParams params;
  params.hardware_info = voltage_sensor_res;
  params.clock = node->get_clock();
  params.logger = node->get_logger();
  auto state = sensor_hw.initialize(params);

  auto state_interfaces = sensor_hw.export_state_interfaces();
  // Updated because is is INACTIVE
  state = sensor_hw.configure();
  state = sensor_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  ASSERT_EQ(hardware_interface::return_type::OK, sensor_hw.read(TIME, PERIOD));

  // Initiate recoverable error - call read 99 times OK and on 100-time will return error
  for (auto i = 2ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, sensor_hw.read(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, sensor_hw.read(TIME, PERIOD));

  state = sensor_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  // activate again and expect reset values
  auto si_joint1_vol = test_components::vector_contains(state_interfaces, "sens1/voltage").second;
  state = sensor_hw.configure();
  EXPECT_EQ(state_interfaces[si_joint1_vol]->get_optional().value(), 0.0);

  state = sensor_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  // Initiate unrecoverable error - call read 99 times OK and on 100-time will return error
  for (auto i = 1ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, sensor_hw.read(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, sensor_hw.read(TIME, PERIOD));

  state = sensor_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());

  // can not change state anymore
  state = sensor_hw.configure();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());
}

// BEGIN (Handle export change): for backward compatibility
TEST(TestComponentInterfaces, dummy_system_read_error_behavior)
{
  hardware_interface::System system_hw(std::make_unique<test_components::DummySystem>());

  hardware_interface::HardwareInfo mock_hw_info{};
  rclcpp::Node::SharedPtr node = std::make_shared<rclcpp::Node>("test_system_components");
  hardware_interface::HardwareComponentParams params;
  params.hardware_info = mock_hw_info;
  params.clock = node->get_clock();
  params.logger = node->get_logger();
  auto state = system_hw.initialize(params);
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  auto state_interfaces = system_hw.export_state_interfaces();
  auto command_interfaces = system_hw.export_command_interfaces();
  state = system_hw.configure();
  state = system_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));
  ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));

  // Initiate error on write (this is first time therefore recoverable)
  for (auto i = 2ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, system_hw.read(TIME, PERIOD));

  state = system_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  // activate again and expect reset values
  state = system_hw.configure();
  for (auto index = 0ul; index < 6; ++index)
  {
    EXPECT_EQ(state_interfaces[index]->get_optional().value(), 0.0);
  }
  for (auto index = 0ul; index < 3; ++index)
  {
    EXPECT_EQ(command_interfaces[index]->get_optional().value(), 0.0);
  }
  state = system_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));
  ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));

  // Initiate error on write (this is the second time therefore unrecoverable)
  for (auto i = 2ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, system_hw.read(TIME, PERIOD));

  state = system_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());

  // can not change state anymore
  state = system_hw.configure();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());
}
// END

TEST(TestComponentInterfaces, dummy_system_default_read_error_behavior)
{
  hardware_interface::System system_hw(std::make_unique<test_components::DummySystemDefault>());

  const std::string urdf_to_test =
    std::string(ros2_control_test_assets::urdf_head) +
    ros2_control_test_assets::valid_urdf_ros2_control_dummy_system_robot +
    ros2_control_test_assets::urdf_tail;
  const std::vector<hardware_interface::HardwareInfo> control_resources =
    hardware_interface::parse_control_resources_from_urdf(urdf_to_test);
  const hardware_interface::HardwareInfo dummy_system = control_resources[0];
  rclcpp::Node::SharedPtr node = std::make_shared<rclcpp::Node>("test_system_components");
  hardware_interface::HardwareComponentParams params;
  params.hardware_info = dummy_system;
  params.clock = node->get_clock();
  params.logger = node->get_logger();
  auto state = system_hw.initialize(params);
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  auto state_interfaces = system_hw.export_state_interfaces();
  auto command_interfaces = system_hw.export_command_interfaces();
  state = system_hw.configure();
  state = system_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));
  ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));

  // Initiate error on write (this is first time therefore recoverable)
  for (auto i = 2ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, system_hw.read(TIME, PERIOD));

  state = system_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  // activate again and expect reset values
  state = system_hw.configure();
  for (auto index = 0ul; index < 6; ++index)
  {
    EXPECT_EQ(state_interfaces[index]->get_optional().value(), 0.0);
  }
  for (auto index = 0ul; index < 3; ++index)
  {
    EXPECT_EQ(command_interfaces[index]->get_optional().value(), 0.0);
  }
  state = system_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));
  ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));

  // Initiate error on write (this is the second time therefore unrecoverable)
  for (auto i = 2ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, system_hw.read(TIME, PERIOD));

  state = system_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());

  // can not change state anymore
  state = system_hw.configure();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());
}

// BEGIN (Handle export change): for backward compatibility
TEST(TestComponentInterfaces, dummy_system_write_error_behavior)
{
  hardware_interface::System system_hw(std::make_unique<test_components::DummySystem>());

  hardware_interface::HardwareInfo mock_hw_info{};
  rclcpp::Node::SharedPtr node = std::make_shared<rclcpp::Node>("test_system_components");
  hardware_interface::HardwareComponentParams params;
  params.hardware_info = mock_hw_info;
  params.clock = node->get_clock();
  params.logger = node->get_logger();
  auto state = system_hw.initialize(params);
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  auto state_interfaces = system_hw.export_state_interfaces();
  auto command_interfaces = system_hw.export_command_interfaces();
  state = system_hw.configure();
  state = system_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));
  ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));

  // Initiate error on write (this is first time therefore recoverable)
  for (auto i = 2ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, system_hw.write(TIME, PERIOD));

  state = system_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  // activate again and expect reset values
  state = system_hw.configure();
  for (auto index = 0ul; index < 6; ++index)
  {
    EXPECT_EQ(state_interfaces[index]->get_optional().value(), 0.0);
  }
  for (auto index = 0ul; index < 3; ++index)
  {
    EXPECT_EQ(command_interfaces[index]->get_optional().value(), 0.0);
  }
  state = system_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));
  ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));

  // Initiate error on write (this is the second time therefore unrecoverable)
  for (auto i = 2ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, system_hw.write(TIME, PERIOD));

  state = system_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());

  // can not change state anymore
  state = system_hw.configure();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());
}
// END

TEST(TestComponentInterfaces, dummy_system_default_write_error_behavior)
{
  hardware_interface::System system_hw(std::make_unique<test_components::DummySystemDefault>());

  const std::string urdf_to_test =
    std::string(ros2_control_test_assets::urdf_head) +
    ros2_control_test_assets::valid_urdf_ros2_control_dummy_system_robot +
    ros2_control_test_assets::urdf_tail;
  const std::vector<hardware_interface::HardwareInfo> control_resources =
    hardware_interface::parse_control_resources_from_urdf(urdf_to_test);
  const hardware_interface::HardwareInfo dummy_system = control_resources[0];
  rclcpp::Node::SharedPtr node = std::make_shared<rclcpp::Node>("test_system_components");
  hardware_interface::HardwareComponentParams params;
  params.hardware_info = dummy_system;
  params.clock = node->get_clock();
  params.logger = node->get_logger();
  auto state = system_hw.initialize(params);
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  auto state_interfaces = system_hw.export_state_interfaces();
  auto command_interfaces = system_hw.export_command_interfaces();
  state = system_hw.configure();
  state = system_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));
  ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));

  // Initiate error on write (this is first time therefore recoverable)
  for (auto i = 2ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, system_hw.write(TIME, PERIOD));

  state = system_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::UNCONFIGURED, state.label());

  // activate again and expect reset values
  state = system_hw.configure();
  for (auto index = 0ul; index < 6; ++index)
  {
    EXPECT_EQ(state_interfaces[index]->get_optional().value(), 0.0);
  }
  for (auto index = 0ul; index < 3; ++index)
  {
    EXPECT_EQ(command_interfaces[index]->get_optional().value(), 0.0);
  }
  state = system_hw.activate();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::ACTIVE, state.label());

  ASSERT_EQ(hardware_interface::return_type::OK, system_hw.read(TIME, PERIOD));
  ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));

  // Initiate error on write (this is the second time therefore unrecoverable)
  for (auto i = 2ul; i < TRIGGER_READ_WRITE_ERROR_CALLS; ++i)
  {
    ASSERT_EQ(hardware_interface::return_type::OK, system_hw.write(TIME, PERIOD));
  }
  ASSERT_EQ(hardware_interface::return_type::ERROR, system_hw.write(TIME, PERIOD));

  state = system_hw.get_lifecycle_state();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());

  // can not change state anymore
  state = system_hw.configure();
  EXPECT_EQ(lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED, state.id());
  EXPECT_EQ(hardware_interface::lifecycle_state_names::FINALIZED, state.label());
}

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  testing::InitGoogleMock(&argc, argv);
  return RUN_ALL_TESTS();
}
