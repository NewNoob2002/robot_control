#pragma once

#include "platform/linux/error.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace robot_control::communication::canopen {

/** Startup values required before any later CANopen lifecycle activation. */
struct StackConfig {
  std::string interface_name{};
  std::uint8_t controller_node_id{0};
  std::uint8_t remote_node_id{1};
  std::uint16_t bit_rate_kbit_s{500};
  std::chrono::milliseconds heartbeat_timeout{0};
  std::chrono::milliseconds sdo_timeout{0};
};

/**
 * Validate injected CANopen startup configuration without system calls.
 *
 * @param config Candidate configuration owned by the caller.
 * @return Success for values representable by the pinned stack, otherwise a
 * context-rich validation error.
 *
 * Thread safety: Pure and reentrant. No interface lookup or activation occurs.
 */
[[nodiscard]] platform::linux::Status
validate_stack_config(const StackConfig &config) noexcept;

} // namespace robot_control::communication::canopen
