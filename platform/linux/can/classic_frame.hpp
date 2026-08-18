#pragma once

#include "platform/linux/error.hpp"

#include <linux/can.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace robot_control::platform::linux::can {

/** Policy-free representation of one Linux Classical CAN frame. */
struct ClassicCanFrame {
  std::uint32_t raw_can_id{0};
  std::uint8_t payload_length{0};
  std::uint8_t len8_dlc{0};
  std::array<std::byte, CAN_MAX_DLEN> data{};
};

/**
 * Validate a Classical CAN frame before transmission.
 *
 * Error frames are observations produced by the kernel and are rejected for
 * transmission by this API. Standard, extended, and remote-request identifiers
 * retain their Linux CAN flag representation.
 *
 * @param frame Frame to validate without modifying it.
 * @return Success or `EINVAL` with the raw identifier in the context.
 *
 * Thread safety: Pure and reentrant.
 */
[[nodiscard]] Status
validate_transmit_frame(const ClassicCanFrame &frame) noexcept;

/**
 * Encode a validated project frame into the Linux kernel ABI structure.
 *
 * @param frame Frame whose storage remains owned by the caller.
 * @return Zero-initialized kernel frame or a validation failure.
 *
 * Thread safety: Pure and reentrant.
 */
[[nodiscard]] Result<::can_frame>
encode_classic_frame(const ClassicCanFrame &frame) noexcept;

/**
 * Decode one Linux kernel ABI frame while preserving its raw CAN identifier.
 *
 * Error-frame class bits and the optional raw DLC are retained for diagnostics.
 * Malformed flag combinations or lengths are rejected.
 *
 * @param frame Kernel frame copied from a complete SocketCAN read.
 * @return Project frame or `EINVAL` with raw frame context.
 *
 * Thread safety: Pure and reentrant.
 */
[[nodiscard]] Result<ClassicCanFrame>
decode_classic_frame(const ::can_frame &frame) noexcept;

} // namespace robot_control::platform::linux::can
