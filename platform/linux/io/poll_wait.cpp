#include "platform/linux/io/poll_wait.hpp"

#include <poll.h>

#include <array>
#include <cerrno>
#include <climits>

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
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  int result = 0;
  do {
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
    const auto remaining_count = remaining.count() > 0 ? remaining.count() : 0;
    result =
        ::poll(descriptors.data(), count, static_cast<int>(remaining_count));
  } while (result < 0 && errno == EINTR);

  if (result < 0) {
    return Result<PollResult>::failure(
        Status::from_errno("poll", "fd=" + std::to_string(fd), errno));
  }
  return Result<PollResult>::success(PollResult{
      .readable = (descriptors[0].revents & POLLIN) != 0,
      .hangup = (descriptors[0].revents & POLLHUP) != 0,
      .error = (descriptors[0].revents & (POLLERR | POLLNVAL)) != 0,
      .cancelled = cancellation_fd >= 0 &&
                   (descriptors[1].revents &
                    (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0,
  });
}

} // namespace robot_control::platform::linux::io
