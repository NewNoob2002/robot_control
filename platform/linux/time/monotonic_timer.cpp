#include "platform/linux/time/monotonic_timer.hpp"

#include <time.h>

#include <cerrno>

namespace robot_control::platform::linux::time {
namespace {

/**
 * Convert nanoseconds to a normalized POSIX timespec.
 *
 * @param value Nonnegative nanosecond timestamp.
 * @return Normalized timespec.
 */
timespec to_timespec(const Timestamp value) noexcept {
  constexpr std::int64_t nanoseconds_per_second = 1'000'000'000;
  return timespec{
      .tv_sec = static_cast<time_t>(value.count() / nanoseconds_per_second),
      .tv_nsec =
          static_cast<long>(value.count() % nanoseconds_per_second), // NOLINT
  };
}

} // namespace

Result<Timestamp> now() noexcept {
  timespec value{};
  if (::clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
    return Result<Timestamp>::failure(
        Status::from_errno("clock_gettime", "CLOCK_MONOTONIC", errno));
  }
  return Result<Timestamp>::success(std::chrono::seconds{value.tv_sec} +
                                    std::chrono::nanoseconds{value.tv_nsec});
}

Status sleep_until(const Timestamp deadline) noexcept {
  if (deadline < Timestamp::zero()) {
    return Status::from_errno("clock_nanosleep", "negative deadline", EINVAL);
  }
  const auto target = to_timespec(deadline);
  int result = 0;
  do {
    result =
        ::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target, nullptr);
  } while (result == EINTR);
  if (result != 0) {
    return Status::from_errno("clock_nanosleep", "CLOCK_MONOTONIC", result);
  }
  return Status::success();
}

PeriodicDeadline::PeriodicDeadline(const Timestamp first,
                                   const Timestamp period) noexcept
    : next_{first}, period_{period} {}

Result<std::uint64_t> PeriodicDeadline::wait_next() noexcept {
  if (period_ <= Timestamp::zero()) {
    return Result<std::uint64_t>::failure(
        Status::from_errno("periodic_wait", "non-positive period", EINVAL));
  }
  const auto status = sleep_until(next_);
  if (!status.ok()) {
    return Result<std::uint64_t>::failure(status);
  }
  const auto wake = now();
  if (!wake.ok()) {
    return Result<std::uint64_t>::failure(wake.status());
  }

  std::uint64_t missed = 0;
  do {
    next_ += period_;
    if (next_ <= wake.value()) {
      ++missed;
    }
  } while (next_ <= wake.value());
  return Result<std::uint64_t>::success(missed);
}

} // namespace robot_control::platform::linux::time
