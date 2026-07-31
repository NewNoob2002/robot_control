#pragma once

#include <chrono>

namespace robot_control::domain::time {

using MonotonicClock = std::chrono::steady_clock;
using MonotonicTime = MonotonicClock::time_point;
using Duration = MonotonicClock::duration;

/**
 * Return whether a timestamp is no older than a configured timeout.
 *
 * Future timestamps fail closed because producer and consumer must share the
 * same injected monotonic time domain.
 *
 * @param now Current monotonic time.
 * @param timestamp Timestamp to evaluate.
 * @param timeout Maximum permitted age; negative values are invalid.
 * @return True only when timestamp is not in the future and its age is within
 * timeout.
 *
 * Thread safety: Pure and reentrant.
 */
[[nodiscard]] constexpr bool is_fresh(const MonotonicTime now,
                                      const MonotonicTime timestamp,
                                      const Duration timeout) noexcept {
  return timeout >= Duration::zero() && timestamp <= now &&
         (now - timestamp) <= timeout;
}

} // namespace robot_control::domain::time
