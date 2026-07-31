#include "platform/linux/io/poll_wait.hpp"

#include <poll.h>

#include <array>
#include <cerrno>
#include <climits>
#include <time.h>

namespace robot_control::platform::linux::io {

Result<PollResult>
wait_readable(const int fd, const std::chrono::milliseconds timeout) noexcept {
  return wait_readable(fd, timeout, -1);
}

Result<PollResult> wait_readable(const int fd,
                                 const std::chrono::milliseconds timeout,
                                 const int cancellation_fd) noexcept {
  if (fd < 0 || cancellation_fd == fd || timeout.count() < 0 ||
      timeout.count() > INT_MAX) {
    return Result<PollResult>::failure(
        Status::from_errno("poll", "fd", EINVAL));
  }

  std::array<pollfd, 2> descriptors{{
      {.fd = fd, .events = POLLIN, .revents = 0},
      {.fd = cancellation_fd, .events = POLLIN, .revents = 0},
  }};
  const nfds_t count = cancellation_fd >= 0 ? 2U : 1U;
  const auto start = std::chrono::steady_clock::now();
  const auto wait_duration =
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout);
  if (wait_duration >
      std::chrono::steady_clock::time_point::max().time_since_epoch() -
          start.time_since_epoch()) {
    return Result<PollResult>::failure(
        Status::from_errno("ppoll", "timeout overflow", EOVERFLOW));
  }
  const auto deadline = start + wait_duration;
  int result = 0;
  do {
    const auto remaining = deadline - std::chrono::steady_clock::now();
    const auto bounded = remaining > std::chrono::steady_clock::duration::zero()
                             ? remaining
                             : std::chrono::steady_clock::duration::zero();
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(bounded);
    const auto nanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(bounded - seconds);
    const timespec wait{
        .tv_sec = static_cast<time_t>(seconds.count()),
        .tv_nsec = static_cast<long>(nanoseconds.count()),
    };
    result = ::ppoll(descriptors.data(), count, &wait, nullptr);
  } while (result < 0 && errno == EINTR);

  if (result < 0) {
    return Result<PollResult>::failure(
        Status::from_errno("ppoll", "fd=" + std::to_string(fd), errno));
  }
  if ((descriptors[0].revents & POLLNVAL) != 0) {
    return Result<PollResult>::failure(
        Status::from_errno("ppoll", "invalid fd=" + std::to_string(fd), EBADF));
  }
  if (cancellation_fd >= 0) {
    if ((descriptors[1].revents & POLLNVAL) != 0) {
      return Result<PollResult>::failure(Status::from_errno(
          "poll", "invalid cancellation fd=" + std::to_string(cancellation_fd),
          EBADF));
    }
    if ((descriptors[1].revents & (POLLHUP | POLLERR)) != 0) {
      return Result<PollResult>::failure(Status::from_errno(
          "poll", "failed cancellation fd=" + std::to_string(cancellation_fd),
          EIO));
    }
  }
  return Result<PollResult>::success(PollResult{
      .readable = (descriptors[0].revents & POLLIN) != 0,
      .hangup = (descriptors[0].revents & POLLHUP) != 0,
      .error = (descriptors[0].revents & POLLERR) != 0,
      .cancelled =
          cancellation_fd >= 0 && (descriptors[1].revents & POLLIN) != 0,
  });
}

} // namespace robot_control::platform::linux::io
