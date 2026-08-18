#pragma once

#include "platform/linux/error.hpp"

#include <chrono>
#include <cstdint>

namespace robot_control::platform::linux::io {

struct PollResult {
  bool readable{false};
  bool writable{false};
  bool hangup{false};
  bool error{false};
  bool cancelled{false};
};

/**
 * Wait for readable, hangup, or error events on one descriptor.
 *
 * @param fd Borrowed descriptor; ownership remains with the caller.
 * @param timeout Nonnegative maximum wait duration.
 * @param cancellation_fd Optional borrowed cancellation descriptor. Its
 * readiness returns `cancelled=true` without consuming the event.
 * @return Poll flags, timeout with all flags false, or context-rich failure.
 *
 * Thread safety: Reentrant for distinct descriptors. The caller owns descriptor
 * lifetime for the complete call.
 */
[[nodiscard]] Result<PollResult>
wait_readable(int fd, std::chrono::milliseconds timeout) noexcept;
[[nodiscard]] Result<PollResult>
wait_readable(int fd, std::chrono::milliseconds timeout,
              int cancellation_fd) noexcept;

/**
 * Wait until an absolute monotonic deadline for readable socket events.
 *
 * @param fd Borrowed descriptor; ownership remains with the caller.
 * @param deadline Absolute `steady_clock` deadline. A past deadline performs
 * an immediate readiness check.
 * @param cancellation_fd Optional borrowed cancellation descriptor, or -1.
 * Its readiness returns `cancelled=true` without consuming the event.
 * @return Poll flags, timeout with all flags false, or context-rich failure.
 *
 * Thread safety: Reentrant for distinct descriptors. The caller owns both
 * descriptor lifetimes for the complete call.
 */
[[nodiscard]] Result<PollResult>
wait_readable_until(int fd, std::chrono::steady_clock::time_point deadline,
                    int cancellation_fd) noexcept;

/**
 * Wait for writable, hangup, or error events on one descriptor.
 *
 * @param fd Borrowed descriptor; ownership remains with the caller.
 * @param timeout Nonnegative maximum wait duration.
 * @param cancellation_fd Optional borrowed cancellation descriptor. Its
 * readiness returns `cancelled=true` without consuming the event.
 * @return Poll flags, timeout with all flags false, or context-rich failure.
 *
 * Thread safety: Reentrant for distinct descriptors. The caller owns descriptor
 * lifetime for the complete call.
 */
[[nodiscard]] Result<PollResult>
wait_writable(int fd, std::chrono::milliseconds timeout) noexcept;
[[nodiscard]] Result<PollResult>
wait_writable(int fd, std::chrono::milliseconds timeout,
              int cancellation_fd) noexcept;

/**
 * Wait until an absolute monotonic deadline for writable socket events.
 *
 * @param fd Borrowed descriptor; ownership remains with the caller.
 * @param deadline Absolute `steady_clock` deadline. A past deadline performs
 * an immediate readiness check.
 * @param cancellation_fd Optional borrowed cancellation descriptor, or -1.
 * Its readiness returns `cancelled=true` without consuming the event.
 * @return Poll flags, timeout with all flags false, or context-rich failure.
 *
 * Thread safety: Reentrant for distinct descriptors. The caller owns both
 * descriptor lifetimes for the complete call.
 */
[[nodiscard]] Result<PollResult>
wait_writable_until(int fd, std::chrono::steady_clock::time_point deadline,
                    int cancellation_fd) noexcept;

} // namespace robot_control::platform::linux::io
