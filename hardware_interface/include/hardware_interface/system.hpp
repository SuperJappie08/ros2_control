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

#ifndef HARDWARE_INTERFACE__SYSTEM_HPP_
#define HARDWARE_INTERFACE__SYSTEM_HPP_

#include <memory>
#include <string>
#include <vector>

#include "hardware_interface/handle.hpp"
#include "hardware_interface/hardware_info.hpp"
#include "hardware_interface/system_interface.hpp"
#include "hardware_interface/types/hardware_interface_return_values.hpp"
#include "hardware_interface/types/statistics_types.hpp"
#include "rclcpp/duration.hpp"
#include "rclcpp/logger.hpp"
#include "rclcpp/node_interfaces/node_clock_interface.hpp"
#include "rclcpp/time.hpp"
#include "rclcpp_lifecycle/state.hpp"

namespace hardware_interface
{
class SystemInterface;

class System final
{
public:
  System() = default;

  explicit System(std::unique_ptr<SystemInterface> impl);

  explicit System(System && other) noexcept;

  ~System() = default;

  [[deprecated(
    "Replaced by const rclcpp_lifecycle::State & initialize(const "
    "hardware_interface::HardwareComponentParams & params).")]]
  const rclcpp_lifecycle::State & initialize(
    const HardwareInfo & system_info, rclcpp::Logger logger,
    rclcpp::node_interfaces::NodeClockInterface::SharedPtr clock_interface);

  [[deprecated(
    "Replaced by const rclcpp_lifecycle::State & initialize(const "
    "hardware_interface::HardwareComponentParams & params).")]]
  const rclcpp_lifecycle::State & initialize(
    const HardwareInfo & system_info, rclcpp::Logger logger, rclcpp::Clock::SharedPtr clock);

  const rclcpp_lifecycle::State & initialize(
    const hardware_interface::HardwareComponentParams & params);

  const rclcpp_lifecycle::State & configure();

  const rclcpp_lifecycle::State & cleanup();

  const rclcpp_lifecycle::State & shutdown();

  const rclcpp_lifecycle::State & activate();

  const rclcpp_lifecycle::State & deactivate();

  const rclcpp_lifecycle::State & error();

  std::vector<StateInterface::ConstSharedPtr> export_state_interfaces();

  std::vector<CommandInterface::SharedPtr> export_command_interfaces();

  return_type prepare_command_mode_switch(
    const std::vector<std::string> & start_interfaces,
    const std::vector<std::string> & stop_interfaces);

  return_type perform_command_mode_switch(
    const std::vector<std::string> & start_interfaces,
    const std::vector<std::string> & stop_interfaces);

  const std::string & get_name() const;

  const std::string & get_group_name() const;

  const rclcpp_lifecycle::State & get_lifecycle_state() const;

  const rclcpp::Time & get_last_read_time() const;

  const rclcpp::Time & get_last_write_time() const;

  const HardwareComponentStatisticsCollector & get_read_statistics() const;

  const HardwareComponentStatisticsCollector & get_write_statistics() const;

  return_type read(const rclcpp::Time & time, const rclcpp::Duration & period);

  return_type write(const rclcpp::Time & time, const rclcpp::Duration & period);

  std::recursive_mutex & get_mutex();

private:
  std::unique_ptr<SystemInterface> impl_;
  mutable std::recursive_mutex system_mutex_;
  // Last read cycle time
  rclcpp::Time last_read_cycle_time_;
  // Last write cycle time
  rclcpp::Time last_write_cycle_time_;
  // Component statistics
  HardwareComponentStatisticsCollector read_statistics_;
  HardwareComponentStatisticsCollector write_statistics_;
};

}  // namespace hardware_interface
#endif  // HARDWARE_INTERFACE__SYSTEM_HPP_
