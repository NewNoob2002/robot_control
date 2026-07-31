#pragma once

#include "platform/linux/error.hpp"
#include "platform/linux/unique_fd.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace robot_control::platform::linux::uart {

enum class BaudRate : std::uint32_t {
  baud_9600 = 9'600,
  baud_19200 = 19'200,
  baud_38400 = 38'400,
  baud_57600 = 57'600,
  baud_115200 = 115'200,
};

struct SerialConfig {
  BaudRate baud_rate{BaudRate::baud_115200};
  bool two_stop_bits{false};
  bool even_parity{false};
};

class SerialPort final {
public:
  /** Construct a closed serial port. */
  SerialPort() noexcept = default;

  SerialPort(const SerialPort &) = delete;
  SerialPort &operator=(const SerialPort &) = delete;
  SerialPort(SerialPort &&) noexcept = default;
  SerialPort &operator=(SerialPort &&) noexcept = default;

  /**
   * Open and configure a tty in nonblocking raw mode.
   *
   * @param path Stable device path or PTY path.
   * @param config Baud, stop-bit, and parity configuration.
   * @return Owned serial port or context-rich open/termios failure.
   */
  [[nodiscard]] static Result<SerialPort> open(std::string path,
                                               SerialConfig config) noexcept;

  /**
   * Wait for bytes and read one bounded chunk.
   *
   * @param destination Caller-owned output buffer valid for the complete call.
   * @param timeout Nonnegative maximum wait.
   * @return Byte count, zero on timeout, or poll/read/disconnect failure.
   *
   * Thread safety: Single-reader only.
   */
  [[nodiscard]] Result<std::size_t>
  read_some(std::span<std::byte> destination,
            std::chrono::milliseconds timeout) noexcept;

  /**
   * Wait for bytes while also observing a cancellation descriptor.
   *
   * @param destination Caller-owned output buffer.
   * @param timeout Nonnegative maximum wait.
   * @param cancellation_fd Borrowed event descriptor.
   * @return Byte count, timeout, cancellation error, or device failure.
   */
  [[nodiscard]] Result<std::size_t> read_some(std::span<std::byte> destination,
                                              std::chrono::milliseconds timeout,
                                              int cancellation_fd) noexcept;

  /**
   * Return the borrowed tty descriptor.
   *
   * @return Descriptor or -1 when closed.
   */
  [[nodiscard]] int fd() const noexcept { return fd_.get(); }

  /**
   * Return the configured device identity.
   *
   * @return Path reference with lifetime tied to this port.
   */
  [[nodiscard]] const std::string &path() const noexcept { return path_; }

private:
  SerialPort(UniqueFd fd, std::string path) noexcept;

  UniqueFd fd_{};
  std::string path_{};
};

} // namespace robot_control::platform::linux::uart
