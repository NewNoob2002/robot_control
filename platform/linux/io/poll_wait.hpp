#pragma once

#include "platform/linux/error.hpp"

#include <chrono>
#include <cstdint>

namespace robot_control::platform::linux::io {

struct PollResult {
  bool readable{false};
  bool hangup{false};
  bool error{false};
};

/**
 * Wait for readable, hangup, or error events on one descriptor.
 *
 * @param fd Borrowed descriptor; ownership remains with the caller.
 * @param timeout Nonnegative maximum wait duration.
 * @return Poll flags, timeout with all flags false, or context-rich failure.
 *
 * Thread safety: Reentrant for distinct descriptors. The caller owns descriptor
 * lifetime for the complete call.
 */
[[nodiscard]] Result<PollResult>
wait_readable(int fd, std::chrono::milliseconds timeout) noexcept;

} // namespace robot_control::platform::linux::io
