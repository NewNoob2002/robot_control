#pragma once

#include "platform/linux/error.hpp"
#include "platform/linux/unique_fd.hpp"

#include <linux/can.h>

#include <optional>
#include <span>
#include <string>

namespace robot_control::platform::linux::can {

/** Configuration borrowed only while opening a Classical CAN RAW socket. */
struct CanSocketConfig {
  /**
   * Optional kernel receive filters.
   *
   * `std::nullopt` preserves the kernel receive-all default, an empty span
   * disables ordinary CAN frame reception, and a nonempty span installs the
   * supplied raw Linux filters. The caller retains ownership of the span and
   * its elements, which need only remain valid for `CanSocket::open()`.
   */
  std::optional<std::span<const ::can_filter>> filters{std::nullopt};

  /** Raw Linux CAN error classes to subscribe to; zero disables error frames.
   */
  can_err_mask_t error_mask{0};
};

/** Move-only owner of one bound Classical CAN RAW socket. */
class CanSocket final {
public:
  /** Construct a closed SocketCAN owner. */
  CanSocket() noexcept = default;

  /** Copying descriptor ownership is forbidden. */
  CanSocket(const CanSocket &) = delete;

  /** Copy assignment is forbidden. */
  CanSocket &operator=(const CanSocket &) = delete;

  /** Move socket ownership and bound identity from another owner. */
  CanSocket(CanSocket &&) noexcept = default;

  /** Replace this socket by moving ownership and identity from another owner.
   */
  CanSocket &operator=(CanSocket &&) noexcept = default;

  /**
   * Open, configure, and bind a nonblocking Classical CAN RAW socket.
   *
   * @param interface_name Linux network-interface name copied into the owner.
   * @param config Receive-filter and error-mask configuration. Filter storage
   * remains caller-owned and is borrowed only for this call.
   * @return Bound socket owner or a failure containing the syscall operation,
   * interface identity, and captured errno.
   *
   * Thread safety: Safe for concurrent calls with independent arguments.
   */
  [[nodiscard]] static Result<CanSocket>
  open(std::string interface_name, CanSocketConfig config = {}) noexcept;

  /**
   * Return the borrowed socket descriptor.
   *
   * @return Descriptor or -1 when closed or moved from. Ownership remains with
   * this object; the descriptor is valid until this owner is moved or
   * destroyed.
   *
   * Thread safety: Safe for concurrent reads while the owner is not moved or
   * destroyed.
   */
  [[nodiscard]] int fd() const noexcept { return fd_.get(); }

  /**
   * Return the bound interface name.
   *
   * @return Reference with lifetime tied to this owner.
   *
   * Thread safety: Safe for concurrent reads while the owner is not moved or
   * destroyed.
   */
  [[nodiscard]] const std::string &interface_name() const noexcept {
    return interface_name_;
  }

  /**
   * Return the bound Linux interface index.
   *
   * @return Interface index, or zero for a closed default-constructed owner.
   *
   * Thread safety: Safe for concurrent reads while the owner is not moved or
   * destroyed.
   */
  [[nodiscard]] unsigned int interface_index() const noexcept {
    return interface_index_;
  }

private:
  /** Construct an owner from a configured descriptor and resolved identity. */
  CanSocket(UniqueFd fd, std::string interface_name,
            unsigned int interface_index) noexcept;

  UniqueFd fd_{};
  std::string interface_name_{};
  unsigned int interface_index_{0};
};

} // namespace robot_control::platform::linux::can
