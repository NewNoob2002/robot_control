#pragma once

#include "platform/linux/error.hpp"

#include <chrono>
#include <cstdint>

namespace robot_control::platform::linux::time {

using Timestamp = std::chrono::nanoseconds;

/**
 * Read CLOCK_MONOTONIC.
 *
 * @return Nanoseconds in the kernel monotonic time domain or a syscall error.
 *
 * Thread safety: Reentrant.
 */
[[nodiscard]] Result<Timestamp> now() noexcept;

/**
 * Sleep until an absolute CLOCK_MONOTONIC deadline.
 *
 * @param deadline Absolute monotonic timestamp.
 * @return Success after the deadline or a context-rich syscall failure.
 *
 * Thread safety: Reentrant; cancellation is by signal interruption followed by
 * the kernel-reported remaining absolute deadline.
 */
[[nodiscard]] Status sleep_until(Timestamp deadline) noexcept;

class PeriodicDeadline final {
public:
  /**
   * Construct an absolute periodic deadline sequence.
   *
   * @param first First absolute deadline.
   * @param period Positive interval between deadlines.
   */
  PeriodicDeadline(Timestamp first, Timestamp period) noexcept;

  /**
   * Sleep until the current deadline and advance by one or more periods.
   *
   * Missed periods are skipped rather than accumulated as relative drift.
   *
   * @return Number of deadlines missed before the wake-up, or a syscall error.
   *
   * Thread safety: Single-owner only.
   */
  [[nodiscard]] Result<std::uint64_t> wait_next() noexcept;

  /**
   * Return the next absolute deadline.
   *
   * @return Monotonic timestamp.
   */
  [[nodiscard]] Timestamp next() const noexcept { return next_; }

private:
  Timestamp next_{};
  Timestamp period_{};
};

} // namespace robot_control::platform::linux::time
