#include "platform/linux/uart/serial_port.hpp"

#include "platform/linux/io/poll_wait.hpp"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

namespace robot_control::platform::linux::uart {
namespace {

/**
 * Translate a supported typed baud rate to termios.
 *
 * @param rate Typed baud rate.
 * @return termios speed constant, or zero when unsupported.
 */
speed_t termios_speed(const BaudRate rate) noexcept {
  switch (rate) {
  case BaudRate::baud_9600:
    return B9600;
  case BaudRate::baud_19200:
    return B19200;
  case BaudRate::baud_38400:
    return B38400;
  case BaudRate::baud_57600:
    return B57600;
  case BaudRate::baud_115200:
    return B115200;
  }
  return 0;
}

} // namespace

SerialPort::SerialPort(UniqueFd fd, std::string path) noexcept
    : fd_{std::move(fd)}, path_{std::move(path)} {}

Result<SerialPort> SerialPort::open(std::string path,
                                    const SerialConfig config) noexcept {
  const int descriptor =
      ::open(path.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  if (descriptor < 0) {
    return Result<SerialPort>::failure(Status::from_errno("open", path, errno));
  }
  UniqueFd fd{descriptor};

  termios attributes{};
  if (::tcgetattr(fd.get(), &attributes) != 0) {
    return Result<SerialPort>::failure(
        Status::from_errno("tcgetattr", path, errno));
  }
  ::cfmakeraw(&attributes);
  attributes.c_cflag |= CLOCAL | CREAD;
  if (config.two_stop_bits) {
    attributes.c_cflag |= CSTOPB;
  } else {
    attributes.c_cflag &= static_cast<tcflag_t>(~CSTOPB);
  }
  if (config.even_parity) {
    attributes.c_cflag |= PARENB;
    attributes.c_cflag &= static_cast<tcflag_t>(~PARODD);
  } else {
    attributes.c_cflag &= static_cast<tcflag_t>(~PARENB);
  }

  const speed_t speed = termios_speed(config.baud_rate);
  if (speed == 0) {
    return Result<SerialPort>::failure(
        Status::from_errno("cfsetspeed", path, EINVAL));
  }
  if (::cfsetispeed(&attributes, speed) != 0) {
    return Result<SerialPort>::failure(
        Status::from_errno("cfsetispeed", path, errno));
  }
  if (::cfsetospeed(&attributes, speed) != 0) {
    return Result<SerialPort>::failure(
        Status::from_errno("cfsetospeed", path, errno));
  }
  if (::tcsetattr(fd.get(), TCSANOW, &attributes) != 0) {
    return Result<SerialPort>::failure(
        Status::from_errno("tcsetattr", path, errno));
  }
  return Result<SerialPort>::success(
      SerialPort{std::move(fd), std::move(path)});
}

Result<std::size_t>
SerialPort::read_some(const std::span<std::byte> destination,
                      const std::chrono::milliseconds timeout) noexcept {
  return read_some(destination, timeout, -1);
}

Result<std::size_t>
SerialPort::read_some(const std::span<std::byte> destination,
                      const std::chrono::milliseconds timeout,
                      const int cancellation_fd) noexcept {
  if (destination.empty()) {
    return Result<std::size_t>::failure(
        Status::from_errno("read", path_, EINVAL));
  }
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (true) {
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
    const auto bounded_remaining = remaining > std::chrono::milliseconds::zero()
                                       ? remaining
                                       : std::chrono::milliseconds::zero();
    const auto event =
        io::wait_readable(fd_.get(), bounded_remaining, cancellation_fd);
    if (!event.ok()) {
      return Result<std::size_t>::failure(event.status());
    }
    if (event.value().cancelled) {
      return Result<std::size_t>::failure(
          Status::from_errno("read", path_ + " cancelled", ECANCELED));
    }
    if (event.value().error) {
      return Result<std::size_t>::failure(
          Status::from_errno("poll", path_, EIO));
    }
    if (!event.value().readable) {
      if (event.value().hangup) {
        return Result<std::size_t>::failure(
            Status::from_errno("read", path_ + " disconnected", EIO));
      }
      return Result<std::size_t>::success(0);
    }

    ssize_t count = 0;
    do {
      count = ::read(fd_.get(), destination.data(), destination.size());
    } while (count < 0 && errno == EINTR);
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (std::chrono::steady_clock::now() >= deadline) {
        return Result<std::size_t>::success(0);
      }
      continue;
    }
    if (count < 0) {
      return Result<std::size_t>::failure(
          Status::from_errno("read", path_, errno));
    }
    if (count == 0) {
      return Result<std::size_t>::failure(
          Status::from_errno("read", path_ + " end-of-file", EIO));
    }
    return Result<std::size_t>::success(static_cast<std::size_t>(count));
  }
}

} // namespace robot_control::platform::linux::uart
