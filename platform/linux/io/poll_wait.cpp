#include "platform/linux/io/poll_wait.hpp"

#include <poll.h>

#include <cerrno>
#include <climits>

namespace robot_control::platform::linux::io {

Result<PollResult>
wait_readable(const int fd, const std::chrono::milliseconds timeout) noexcept {
  if (fd < 0 || timeout.count() < 0 || timeout.count() > INT_MAX) {
    return Result<PollResult>::failure(
        Status::from_errno("poll", "fd", EINVAL));
  }

  pollfd descriptor{
      .fd = fd,
      .events = POLLIN,
      .revents = 0,
  };
  int result = 0;
  do {
    result = ::poll(&descriptor, 1, static_cast<int>(timeout.count()));
  } while (result < 0 && errno == EINTR);

  if (result < 0) {
    return Result<PollResult>::failure(
        Status::from_errno("poll", "fd=" + std::to_string(fd), errno));
  }
  return Result<PollResult>::success(PollResult{
      .readable = (descriptor.revents & POLLIN) != 0,
      .hangup = (descriptor.revents & POLLHUP) != 0,
      .error = (descriptor.revents & (POLLERR | POLLNVAL)) != 0,
  });
}

} // namespace robot_control::platform::linux::io
