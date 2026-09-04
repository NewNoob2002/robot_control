#include "platform/linux/can/classic_frame.hpp"

#include <linux/can.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

using robot_control::platform::linux::can::ClassicCanFrame;
using robot_control::platform::linux::can::decode_classic_frame;
using robot_control::platform::linux::can::encode_classic_frame;
using robot_control::platform::linux::can::validate_transmit_frame;

int failures = 0;

/** Record one frame-codec assertion failure. */
void check(const bool condition, const std::string_view id,
           const std::string_view expression) {
  if (!condition) {
    ++failures;
    std::cerr << id << " failed: " << expression << '\n';
  }
}

#define CHECK(id, expression) check((expression), (id), #expression)

/** Verify standard/extended identifier and payload validation. */
void test_transmit_validation() {
  ClassicCanFrame standard{
      .raw_can_id = CAN_SFF_MASK,
      .payload_length = CAN_MAX_DLEN,
      .len8_dlc = 15,
      .data = {},
  };
  CHECK("CAN-FRAME-001", validate_transmit_frame(standard).ok());

  auto invalid_standard = standard;
  invalid_standard.raw_can_id = CAN_SFF_MASK + 1U;
  const auto invalid_id = validate_transmit_frame(invalid_standard);
  CHECK("CAN-FRAME-002", !invalid_id.ok());
  CHECK("CAN-FRAME-002", invalid_id.error.value() == EINVAL);
  CHECK("CAN-FRAME-002", invalid_id.operation == "validate_can_tx");

  auto extended = standard;
  extended.raw_can_id = CAN_EFF_FLAG | CAN_EFF_MASK;
  CHECK("CAN-FRAME-003", validate_transmit_frame(extended).ok());

  auto oversized = standard;
  oversized.payload_length = CAN_MAX_DLEN + 1U;
  CHECK("CAN-FRAME-004", !validate_transmit_frame(oversized).ok());

  auto invalid_raw_dlc = standard;
  invalid_raw_dlc.len8_dlc = CAN_MAX_DLC;
  CHECK("CAN-FRAME-005", !validate_transmit_frame(invalid_raw_dlc).ok());

  auto error = standard;
  error.raw_can_id = CAN_ERR_FLAG | 1U;
  CHECK("CAN-FRAME-006", !validate_transmit_frame(error).ok());
}

/** Verify deterministic Linux ABI encoding for data and RTR frames. */
void test_encoding() {
  ClassicCanFrame frame{
      .raw_can_id = 0x321U,
      .payload_length = 3U,
      .len8_dlc = 0U,
      .data = {std::byte{0x11}, std::byte{0x22}, std::byte{0x33}},
  };
  const auto encoded = encode_classic_frame(frame);
  CHECK("CAN-FRAME-007", encoded.ok());
  if (encoded.ok()) {
    CHECK("CAN-FRAME-007", encoded.value().can_id == frame.raw_can_id);
    CHECK("CAN-FRAME-007", encoded.value().len == frame.payload_length);
    CHECK("CAN-FRAME-007", encoded.value().data[0] == 0x11U);
    CHECK("CAN-FRAME-007", encoded.value().data[2] == 0x33U);
    CHECK("CAN-FRAME-007", encoded.value().data[3] == 0U);
  }

  frame.raw_can_id |= CAN_RTR_FLAG;
  const auto remote = encode_classic_frame(frame);
  CHECK("CAN-FRAME-008", remote.ok());
  if (remote.ok()) {
    CHECK("CAN-FRAME-008", remote.value().data[0] == 0U);
  }
}

/** Verify receive decoding preserves raw identifiers, error classes, and DLC.
 */
void test_decoding() {
  ::can_frame kernel{};
  kernel.can_id = CAN_ERR_FLAG | 0x20U;
  kernel.len = CAN_MAX_DLEN;
  kernel.len8_dlc = 12U;
  kernel.data[0] = 0xa5U;
  kernel.data[7] = 0x5aU;

  const auto decoded = decode_classic_frame(kernel);
  CHECK("CAN-FRAME-009", decoded.ok());
  if (decoded.ok()) {
    CHECK("CAN-FRAME-009", decoded.value().raw_can_id == kernel.can_id);
    CHECK("CAN-FRAME-009", decoded.value().len8_dlc == kernel.len8_dlc);
    CHECK("CAN-FRAME-009", decoded.value().data[0] == std::byte{0xa5});
    CHECK("CAN-FRAME-009", decoded.value().data[7] == std::byte{0x5a});
  }

  kernel.can_id = CAN_ERR_FLAG | CAN_RTR_FLAG | 1U;
  const auto invalid_flags = decode_classic_frame(kernel);
  CHECK("CAN-FRAME-010", !invalid_flags.ok());
  CHECK("CAN-FRAME-010", invalid_flags.status().error.value() == EINVAL);

  kernel = {};
  kernel.len = CAN_MAX_DLEN + 1U;
  const auto invalid_length = decode_classic_frame(kernel);
  CHECK("CAN-FRAME-011", !invalid_length.ok());
}

} // namespace

/** Run policy-free Classical CAN frame codec tests. */
int main() {
  test_transmit_validation();
  test_encoding();
  test_decoding();
  if (failures != 0) {
    std::cerr << "socketcan_frame_tests failures=" << failures << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "socketcan_frame_tests passed\n";
  return EXIT_SUCCESS;
}
