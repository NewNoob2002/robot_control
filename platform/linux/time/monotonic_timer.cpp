#include "platform/linux/time/monotonic_timer.hpp"

#include <time.h>

#include <cerrno>
#include <limits>
#include <poll.h>

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

Status sleep_until(const Timestamp deadline,
                   const int cancellation_fd) noexcept {
  if (deadline < Timestamp::zero() || cancellation_fd < 0) {
    return Status::from_errno("ppoll", "invalid deadline or cancellation fd",
                              EINVAL);
  }
  pollfd cancellation{
      .fd = cancellation_fd,
      .events = POLLIN,
      .revents = 0,
  };
  while (true) {
    const auto current = now();
    if (!current.ok()) {
      return current.status();
    }
    if (current.value() >= deadline) {
      return Status::success();
    }
    const auto timeout = to_timespec(deadline - current.value());
    const int result = ::ppoll(&cancellation, 1, &timeout, nullptr);
    if (result > 0) {
      if ((cancellation.revents & POLLNVAL) != 0) {
        return Status::from_errno("ppoll", "invalid cancellation fd", EBADF);
      }
      if ((cancellation.revents & (POLLHUP | POLLERR)) != 0) {
        return Status::from_errno("ppoll", "failed cancellation fd", EIO);
      }
      if ((cancellation.revents & POLLIN) != 0) {
        return Status::from_errno("ppoll", "cancelled", ECANCELED);
      }
      return Status::from_errno("ppoll", "unexpected cancellation event", EIO);
    }
    if (result == 0) {
      return Status::success();
    }
    if (errno != EINTR) {
      return Status::from_errno("ppoll", "cancellation fd", errno);
    }
  }
}

PeriodicDeadline::PeriodicDeadline(const Timestamp first,
                                   const Timestamp period) noexcept
    : next_{first}, period_{period} {}

Result<std::uint64_t> PeriodicDeadline::wait_next() noexcept {
  if (period_ <= Timestamp::zero() || next_ < Timestamp::zero()) {
    return Result<std::uint64_t>::failure(Status::from_errno(
        "periodic_wait", "invalid deadline or period", EINVAL));
  }
  const auto status = sleep_until(next_);
  if (!status.ok()) {
    return Result<std::uint64_t>::failure(status);
  }
  return advance_after_wake();
}

Result<std::uint64_t>
PeriodicDeadline::wait_next(const int cancellation_fd) noexcept {
  if (period_ <= Timestamp::zero() || next_ < Timestamp::zero()) {
    return Result<std::uint64_t>::failure(Status::from_errno(
        "periodic_wait", "invalid deadline or period", EINVAL));
  }
  const auto status = sleep_until(next_, cancellation_fd);
  if (!status.ok()) {
    return Result<std::uint64_t>::failure(status);
  }
  return advance_after_wake();
}

Result<std::uint64_t> PeriodicDeadline::advance_after_wake() noexcept {
  if (period_ <= Timestamp::zero() || next_ < Timestamp::zero()) {
    return Result<std::uint64_t>::failure(Status::from_errno(
        "periodic_wait", "invalid deadline or period", EINVAL));
  }
  const auto wake = now();
  if (!wake.ok()) {
    return Result<std::uint64_t>::failure(wake.status());
  }

  if (wake.value() < next_) {
    return Result<std::uint64_t>::failure(
        Status::from_errno("periodic_wait", "clock moved backwards", EIO));
  }
  const auto delta = wake.value().count() - next_.count();
  const auto period = period_.count();
  const auto missed = delta / period;
  const auto remainder = delta % period;
  const auto increment = period - remainder;
  if (wake.value().count() >
      std::numeric_limits<Timestamp::rep>::max() - increment) {
    return Result<std::uint64_t>::failure(
        Status::from_errno("periodic_wait", "deadline overflow", EOVERFLOW));
  }
  next_ = Timestamp{wake.value().count() + increment};
  return Result<std::uint64_t>::success(static_cast<std::uint64_t>(missed));
}

} // namespace robot_control::platform::linux::time
