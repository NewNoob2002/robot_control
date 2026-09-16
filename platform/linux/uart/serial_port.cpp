#include "platform/linux/uart/serial_port.hpp"
#include "platform/linux/io/poll_wait.hpp"

// Kernel termios2 must not be mixed with the different libc termios layout.
#include <asm/termbits.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>

namespace robot_control::platform::linux::uart {
namespace {
/** Translate supported rates; BOTHER carries the explicit 100000 rate. */
unsigned int termios_speed(const BaudRate rate) noexcept {
    switch (rate) {
        case BaudRate::baud_9600:
            return B9600;
        case BaudRate::baud_19200:
            return B19200;
        case BaudRate::baud_38400:
            return B38400;
        case BaudRate::baud_57600:
            return B57600;
        case BaudRate::baud_100000:
            return BOTHER;
        case BaudRate::baud_115200:
            return B115200;
    }
    return 0;
}
} // namespace

SerialPort::SerialPort(UniqueFd fd, std::string path) noexcept : fd_{std::move(fd)}, path_{std::move(path)} {}

Result<SerialPort> SerialPort::open(std::string path, const SerialConfig config) noexcept {
    const auto speed = termios_speed(config.baud_rate);
    if (speed == 0)
        return Result<SerialPort>::failure(Status::from_errno("baud_rate", path, EINVAL));
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (descriptor < 0)
        return Result<SerialPort>::failure(Status::from_errno("open", path, errno));
    SerialPort port{UniqueFd{descriptor}, std::move(path)};
    termios2 attributes{};
    if (::ioctl(port.fd(), TCGETS2, &attributes) != 0)
        return Result<SerialPort>::failure(Status::from_errno("TCGETS2", port.path_, errno));
    attributes.c_iflag = config.mark_errors ? INPCK | PARMRK : 0U;
    attributes.c_oflag = 0;
    attributes.c_lflag = 0;
    attributes.c_cflag = CS8 | CLOCAL | CREAD | speed;
    if (config.two_stop_bits)
        attributes.c_cflag |= CSTOPB;
    if (config.even_parity)
        attributes.c_cflag |= PARENB;
    attributes.c_cc[VMIN] = 1;
    attributes.c_cc[VTIME] = 0;
    attributes.c_ispeed = static_cast<unsigned int>(config.baud_rate);
    attributes.c_ospeed = static_cast<unsigned int>(config.baud_rate);
    if (::ioctl(port.fd(), TCSETS2, &attributes) != 0)
        return Result<SerialPort>::failure(Status::from_errno("TCSETS2", port.path_, errno));
    auto actual = port.configuration();
    if (!actual.ok())
        return Result<SerialPort>::failure(actual.status());
    if (actual.value().baud_rate != config.baud_rate || actual.value().even_parity != config.even_parity
        || actual.value().two_stop_bits != config.two_stop_bits || actual.value().mark_errors != config.mark_errors)
        return Result<SerialPort>::failure(Status::from_errno("UART configuration mismatch", port.path_, ENOTSUP));
    return Result<SerialPort>::success(std::move(port));
}

Result<SerialConfig> SerialPort::configuration() const noexcept {
    termios2 attributes{};
    if (::ioctl(fd(), TCGETS2, &attributes) != 0)
        return Result<SerialConfig>::failure(Status::from_errno("TCGETS2", path_, errno));
    const auto forbidden = PARODD | CMSPAR | CRTSCTS;
    const auto input = attributes.c_iflag;
    if (attributes.c_ispeed != attributes.c_ospeed || (attributes.c_cflag & CSIZE) != CS8
        || (attributes.c_cflag & forbidden) != 0 || (attributes.c_cflag & (CLOCAL | CREAD)) != (CLOCAL | CREAD)
        || attributes.c_line != 0 || attributes.c_lflag != 0 || attributes.c_oflag != 0
        || (input != 0 && input != (INPCK | PARMRK)) || attributes.c_cc[VMIN] != 1 || attributes.c_cc[VTIME] != 0)
        return Result<SerialConfig>::failure(Status::from_errno("UART raw format mismatch", path_, ENOTSUP));
    return Result<SerialConfig>::success(SerialConfig{.baud_rate = static_cast<BaudRate>(attributes.c_ospeed),
                                                      .two_stop_bits = (attributes.c_cflag & CSTOPB) != 0,
                                                      .even_parity = (attributes.c_cflag & PARENB) != 0,
                                                      .mark_errors = (input & PARMRK) != 0});
}

Status SerialPort::discard_input() noexcept {
    if (::ioctl(fd(), TCFLSH, TCIFLUSH) != 0)
        return Status::from_errno("TCFLSH", path_, errno);
    return Status::success();
}

Result<std::size_t> SerialPort::pending_bytes() const noexcept {
    int count = 0;
    if (::ioctl(fd(), FIONREAD, &count) != 0)
        return Result<std::size_t>::failure(Status::from_errno("FIONREAD", path_, errno));
    if (count < 0)
        return Result<std::size_t>::failure(Status::from_errno("FIONREAD", path_, EIO));
    return Result<std::size_t>::success(static_cast<std::size_t>(count));
}

Result<std::size_t> SerialPort::read_some(const std::span<std::byte> destination,
                                          const std::chrono::milliseconds timeout) noexcept {
    return read_some(destination, timeout, -1);
}

Result<std::size_t> SerialPort::read_some(const std::span<std::byte> destination,
                                          const std::chrono::milliseconds timeout, const int cancellation_fd) noexcept {
    if (destination.empty()) {
        return Result<std::size_t>::failure(Status::from_errno("read", path_, EINVAL));
    }
    if (timeout < std::chrono::milliseconds::zero()) {
        return Result<std::size_t>::failure(Status::from_errno("read", path_ + " negative timeout", EINVAL));
    }
    const auto start = std::chrono::steady_clock::now();
    const auto maximum_timeout =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::time_point::max() - start);
    if (timeout > maximum_timeout) {
        return Result<std::size_t>::failure(Status::from_errno("read", path_ + " timeout overflow", EOVERFLOW));
    }
    const auto wait_duration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout);
    const auto deadline = start + wait_duration;
    while (true) {
        const auto remaining = deadline - std::chrono::steady_clock::now();
        const auto bounded_remaining = remaining > std::chrono::steady_clock::duration::zero()
                                           ? std::chrono::ceil<std::chrono::milliseconds>(remaining)
                                           : std::chrono::milliseconds::zero();
        const auto event = io::wait_readable(fd_.get(), bounded_remaining, cancellation_fd);
        if (!event.ok()) {
            auto status = event.status();
            status.context = path_ + " " + status.context;
            return Result<std::size_t>::failure(std::move(status));
        }
        if (event.value().cancelled) {
            return Result<std::size_t>::failure(Status::from_errno("read", path_ + " cancelled", ECANCELED));
        }
        if (event.value().error || event.value().hangup) {
            return Result<std::size_t>::failure(Status::from_errno("poll", path_, EIO));
        }
        if (!event.value().readable) {
            return Result<std::size_t>::success(0);
        }

        ssize_t count = 0;
        do {
            count = ::read(fd_.get(), destination.data(), destination.size());
        } while (count < 0 && errno == EINTR && std::chrono::steady_clock::now() < deadline);
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return Result<std::size_t>::success(0);
            }
            continue;
        }
        if (count < 0) {
            return Result<std::size_t>::failure(Status::from_errno("read", path_, errno));
        }
        if (count == 0) {
            return Result<std::size_t>::failure(Status::from_errno("read", path_ + " end-of-file", EIO));
        }
        return Result<std::size_t>::success(static_cast<std::size_t>(count));
    }
}

} // namespace robot_control::platform::linux::uart
