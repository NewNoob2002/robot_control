#include "platform/linux/can/classic_frame.hpp"

#include <algorithm>
#include <cerrno>
#include <string>
#include <string_view>

namespace robot_control::platform::linux::can {
namespace {

/** Build a validation failure containing raw frame identity. */
Status invalid_frame(const std::string_view operation,
                     const ClassicCanFrame &frame,
                     const std::string_view detail) {
  return Status::from_errno(std::string{operation},
                            "raw_can_id=" + std::to_string(frame.raw_can_id) +
                                " " + std::string{detail},
                            EINVAL);
}

/** Validate representation rules shared by received and transmitted frames. */
Status validate_representation(const std::string_view operation,
                               const ClassicCanFrame &frame) {
  if (frame.payload_length > CAN_MAX_DLEN) {
    return invalid_frame(operation, frame, "payload length exceeds 8");
  }
  if (frame.len8_dlc != 0U &&
      (frame.payload_length != CAN_MAX_DLEN || frame.len8_dlc <= CAN_MAX_DLC ||
       frame.len8_dlc > CAN_MAX_RAW_DLC)) {
    return invalid_frame(operation, frame, "invalid len8_dlc");
  }

  const bool is_extended = (frame.raw_can_id & CAN_EFF_FLAG) != 0U;
  const bool is_remote = (frame.raw_can_id & CAN_RTR_FLAG) != 0U;
  const bool is_error = (frame.raw_can_id & CAN_ERR_FLAG) != 0U;
  if (is_error && (is_extended || is_remote)) {
    return invalid_frame(operation, frame, "error frame has EFF/RTR flag");
  }
  if (!is_error && !is_extended &&
      (frame.raw_can_id & CAN_EFF_MASK) > CAN_SFF_MASK) {
    return invalid_frame(operation, frame,
                         "standard identifier exceeds 11 bits");
  }
  return Status::success();
}

} // namespace

Status validate_transmit_frame(const ClassicCanFrame &frame) noexcept {
  Status representation = validate_representation("validate_can_tx", frame);
  if (!representation.ok()) {
    return representation;
  }
  if ((frame.raw_can_id & CAN_ERR_FLAG) != 0U) {
    return invalid_frame("validate_can_tx", frame,
                         "kernel error frame cannot be transmitted");
  }
  return Status::success();
}

Result<::can_frame>
encode_classic_frame(const ClassicCanFrame &frame) noexcept {
  Status validation = validate_transmit_frame(frame);
  if (!validation.ok()) {
    return Result<::can_frame>::failure(validation);
  }

  ::can_frame encoded{};
  encoded.can_id = frame.raw_can_id;
  encoded.len = frame.payload_length;
  encoded.len8_dlc = frame.len8_dlc;
  if ((frame.raw_can_id & CAN_RTR_FLAG) == 0U) {
    std::copy_n(frame.data.begin(),
                static_cast<std::size_t>(frame.payload_length),
                reinterpret_cast<std::byte *>(encoded.data));
  }
  return Result<::can_frame>::success(encoded);
}

Result<ClassicCanFrame>
decode_classic_frame(const ::can_frame &frame) noexcept {
  ClassicCanFrame decoded{
      .raw_can_id = frame.can_id,
      .payload_length = frame.len,
      .len8_dlc = frame.len8_dlc,
      .data = {},
  };
  std::copy_n(reinterpret_cast<const std::byte *>(frame.data),
              static_cast<std::size_t>(std::min<std::uint8_t>(
                  frame.len, static_cast<std::uint8_t>(CAN_MAX_DLEN))),
              decoded.data.begin());

  Status validation = validate_representation("decode_can_frame", decoded);
  if (!validation.ok()) {
    return Result<ClassicCanFrame>::failure(validation);
  }
  return Result<ClassicCanFrame>::success(decoded);
}

} // namespace robot_control::platform::linux::can
