#pragma once

#include "platform/linux/can/classic_frame.hpp"
#include "platform/linux/error.hpp"
#include "platform/linux/unique_fd.hpp"

#include <linux/can.h>

#include <chrono>
#include <cstdint>
#include <ctime>
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

  /** Enable nanosecond software receive timestamps from the kernel. */
  bool receive_timestamp{false};

  /** Enable raw receive-queue overflow counters from the kernel. */
  bool receive_queue_overflow{false};
};

/** Owned result of receiving one complete Classical CAN frame. */
struct CanReceiveObservation {
  /** Complete decoded frame, including any received `CAN_ERR_FLAG`. */
  ClassicCanFrame frame{};

  /**
   * Optional raw `SCM_TIMESTAMPNS` software timestamp.
   *
   * This timestamp is in the kernel realtime clock domain and is retained for
   * diagnostics only. It must not drive monotonic deadlines, freshness, or
   * safety decisions.
   */
  std::optional<::timespec> kernel_timestamp{};

  /** Optional raw cumulative `SO_RXQ_OVFL` packet-drop counter. */
  std::optional<std::uint32_t> rx_queue_overflow{};
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
   * @param config Receive-filter, error-mask, and optional metadata
   * configuration. Filter storage remains caller-owned and is borrowed only
   * for this call.
   * @return Bound socket owner or a failure containing the syscall operation,
   * interface identity, and captured errno.
   *
   * Thread safety: Safe for concurrent calls with independent arguments.
   */
  [[nodiscard]] static Result<CanSocket>
  open(std::string interface_name, CanSocketConfig config = {}) noexcept;

  /**
   * Send exactly one complete Classical CAN frame before a monotonic deadline.
   *
   * The frame is validated and encoded before any write syscall. Cancellation
   * wins when poll reports both cancellation and socket readiness. A
   * cancellation descriptor is observed but never consumed or closed.
   *
   * @param frame Caller-owned frame borrowed for this call.
   * @param timeout Nonnegative maximum duration for the complete operation.
   * @param cancellation_fd Optional borrowed cancellation descriptor, or -1.
   * @return Success after one full `CAN_MTU` write, or a context-rich failure.
   *
   * Thread safety: The caller must serialize operations using the same socket
   * owner and keep both descriptors alive for the complete call. Cancellation
   * cannot eliminate the race after readiness is returned and before `write()`.
   */
  [[nodiscard]] Status send(const ClassicCanFrame &frame,
                            std::chrono::milliseconds timeout,
                            int cancellation_fd = -1) noexcept;

  /**
   * Receive one complete Classical CAN frame before a monotonic deadline.
   *
   * Cancellation wins when poll reports both cancellation and socket
   * readiness. Readable data is handled before socket error/hangup flags so
   * subscribed CAN error frames remain observable. The cancellation descriptor
   * is observed but never consumed or closed.
   *
   * @param timeout Nonnegative maximum duration for the complete operation.
   * @param cancellation_fd Optional borrowed cancellation descriptor, or -1.
   * @return An owned frame and available kernel metadata, `std::nullopt` on
   * timeout, or a context-rich failure. Missing ancillary metadata remains
   * `std::nullopt`.
   *
   * Thread safety: The caller must serialize operations using the same socket
   * owner and keep both descriptors alive for the complete call. Cancellation
   * cannot eliminate the race after readiness is returned and before
   * `recvmsg()`.
   */
  [[nodiscard]] Result<std::optional<CanReceiveObservation>>
  receive(std::chrono::milliseconds timeout, int cancellation_fd = -1) noexcept;

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
