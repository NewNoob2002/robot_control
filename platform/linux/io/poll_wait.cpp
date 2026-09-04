#include "platform/linux/io/poll_wait.hpp"

#include <poll.h>

#include <array>
#include <cerrno>
#include <climits>
#include <time.h>

namespace robot_control::platform::linux::io {
namespace {

/** Wait for one requested event while observing an absolute deadline. */
Result<PollResult>
wait_for_event(const int fd, const short requested_event,
               const std::chrono::steady_clock::time_point deadline,
               const int cancellation_fd) noexcept {
  if (fd < 0 || cancellation_fd < -1 || cancellation_fd == fd) {
    return Result<PollResult>::failure(
        Status::from_errno("poll", "fd", EINVAL));
  }

  std::array<pollfd, 2> descriptors{{
      {.fd = fd, .events = requested_event, .revents = 0},
      {.fd = cancellation_fd, .events = POLLIN, .revents = 0},
  }};
  const nfds_t count = cancellation_fd >= 0 ? 2U : 1U;
  int result = 0;
  do {
    const auto now = std::chrono::steady_clock::now();
    const auto remaining = deadline > now
                               ? deadline - now
                               : std::chrono::steady_clock::duration::zero();
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(remaining);
    const auto nanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(remaining -
                                                             seconds);
    const timespec wait{
        .tv_sec = static_cast<time_t>(seconds.count()),
        .tv_nsec = static_cast<long>(nanoseconds.count()),
    };
    descriptors[0].revents = 0;
    descriptors[1].revents = 0;
    result = ::ppoll(descriptors.data(), count, &wait, nullptr);
  } while (result < 0 && errno == EINTR);

  if (result < 0) {
    const int saved_errno = errno;
    return Result<PollResult>::failure(
        Status::from_errno("ppoll", "fd=" + std::to_string(fd), saved_errno));
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
    if ((descriptors[1].revents & POLLIN) != 0) {
      return Result<PollResult>::success(PollResult{
          .readable = (descriptors[0].revents & POLLIN) != 0,
          .writable = (descriptors[0].revents & POLLOUT) != 0,
          .hangup = (descriptors[0].revents & POLLHUP) != 0,
          .error = (descriptors[0].revents & POLLERR) != 0,
          .cancelled = true,
      });
    }
    if ((descriptors[1].revents & (POLLHUP | POLLERR)) != 0) {
      return Result<PollResult>::failure(Status::from_errno(
          "poll", "failed cancellation fd=" + std::to_string(cancellation_fd),
          EIO));
    }
  }
  return Result<PollResult>::success(PollResult{
      .readable = (descriptors[0].revents & POLLIN) != 0,
      .writable = (descriptors[0].revents & POLLOUT) != 0,
      .hangup = (descriptors[0].revents & POLLHUP) != 0,
      .error = (descriptors[0].revents & POLLERR) != 0,
      .cancelled = false,
  });
}

/** Convert a nonnegative relative timeout to one absolute deadline. */
Result<std::chrono::steady_clock::time_point>
deadline_after(const std::chrono::milliseconds timeout) noexcept {
  if (timeout.count() < 0 || timeout.count() > INT_MAX) {
    return Result<std::chrono::steady_clock::time_point>::failure(
        Status::from_errno("poll", "fd", EINVAL));
  }
  const auto now = std::chrono::steady_clock::now();
  return Result<std::chrono::steady_clock::time_point>::success(
      now +
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout));
}

} // namespace

Result<PollResult>
wait_readable(const int fd, const std::chrono::milliseconds timeout) noexcept {
  return wait_readable(fd, timeout, -1);
}

Result<PollResult> wait_readable(const int fd,
                                 const std::chrono::milliseconds timeout,
                                 const int cancellation_fd) noexcept {
  const auto deadline = deadline_after(timeout);
  if (!deadline.ok()) {
    return Result<PollResult>::failure(deadline.status());
  }
  return wait_readable_until(fd, deadline.value(), cancellation_fd);
}

Result<PollResult>
wait_readable_until(const int fd,
                    const std::chrono::steady_clock::time_point deadline,
                    const int cancellation_fd) noexcept {
  return wait_for_event(fd, POLLIN, deadline, cancellation_fd);
}

Result<PollResult>
wait_writable(const int fd, const std::chrono::milliseconds timeout) noexcept {
  return wait_writable(fd, timeout, -1);
}

Result<PollResult> wait_writable(const int fd,
                                 const std::chrono::milliseconds timeout,
                                 const int cancellation_fd) noexcept {
  const auto deadline = deadline_after(timeout);
  if (!deadline.ok()) {
    return Result<PollResult>::failure(deadline.status());
  }
  return wait_writable_until(fd, deadline.value(), cancellation_fd);
}

Result<PollResult>
wait_writable_until(const int fd,
                    const std::chrono::steady_clock::time_point deadline,
                    const int cancellation_fd) noexcept {
  return wait_for_event(fd, POLLOUT, deadline, cancellation_fd);
}

} // namespace robot_control::platform::linux::io
