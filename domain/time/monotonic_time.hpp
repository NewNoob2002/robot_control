#pragma once

#include <chrono>
#include <cstdint>

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

/**
 * Check an exclusive age bound in the nonnegative monotonic time domain.
 * @param stamp Producer timestamp; timestamps before the clock epoch are invalid.
 * @param now Current time in the same monotonic clock domain.
 * @param timeout Exclusive maximum age; zero and negative values reject all stamps.
 * @return True only for a nonnegative, nonfuture stamp with age strictly below timeout.
 * Thread safety: Pure and reentrant; no state or ownership is retained.
 */
[[nodiscard]] constexpr bool age_below(MonotonicTime stamp, MonotonicTime now, Duration timeout) noexcept {
    return stamp >= MonotonicTime{} && stamp <= now && now - stamp < timeout;
}

/**
 * Measure diagnostic timestamp age in whole microseconds.
 * @param stamp Timestamp to measure; the default timestamp denotes missing data.
 * @param now Current time in the same monotonic clock domain.
 * @return Whole microseconds of age, or -1 for a default or future timestamp.
 * Thread safety: Pure and reentrant; no state or ownership is retained.
 */
[[nodiscard]] constexpr std::int64_t age_us(MonotonicTime stamp, MonotonicTime now) noexcept {
    return stamp == MonotonicTime{} || stamp > now
               ? -1
               : std::chrono::duration_cast<std::chrono::microseconds>(now - stamp).count();
}

} // namespace robot_control::domain::time
