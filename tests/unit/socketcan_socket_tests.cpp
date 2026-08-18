#include "platform/linux/can/socket.hpp"

#include <linux/can/error.h>

#include <fcntl.h>
#include <net/if.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using namespace std::chrono_literals;
using robot_control::platform::linux::Result;
using robot_control::platform::linux::UniqueFd;
using robot_control::platform::linux::can::CanSocket;
using robot_control::platform::linux::can::CanSocketConfig;
using robot_control::platform::linux::can::ClassicCanFrame;

static_assert(!std::is_copy_constructible_v<CanSocket>);
static_assert(!std::is_copy_assignable_v<CanSocket>);
static_assert(std::is_nothrow_move_constructible_v<CanSocket>);
static_assert(std::is_nothrow_move_assignable_v<CanSocket>);

int failures = 0;

/** Record one SocketCAN lifecycle assertion failure. */
void check(const bool condition, const std::string_view id,
           const std::string_view expression) {
  if (!condition) {
    ++failures;
    std::cerr << id << " failed: " << expression << '\n';
  }
}

#define CHECK(id, expression) check((expression), (id), #expression)

/** Build one valid standard Classical CAN data frame for socket tests. */
ClassicCanFrame test_frame(const std::uint32_t raw_can_id = 0x321U) {
  return ClassicCanFrame{
      .raw_can_id = raw_can_id,
      .payload_length = 4U,
      .len8_dlc = 0U,
      .data = {std::byte{0x11}, std::byte{0x22}, std::byte{0x33},
               std::byte{0x44}},
  };
}

/** Verify metadata configuration defaults and nested optional success. */
void test_metadata_config_and_nested_result() {
  const CanSocketConfig defaults{};
  CHECK("CAN-SOCKET-METADATA-001", !defaults.receive_timestamp);
  CHECK("CAN-SOCKET-METADATA-001", !defaults.receive_queue_overflow);

  const CanSocketConfig enabled{.receive_timestamp = true,
                                .receive_queue_overflow = true};
  CHECK("CAN-SOCKET-METADATA-002", enabled.receive_timestamp);
  CHECK("CAN-SOCKET-METADATA-002", enabled.receive_queue_overflow);

  const auto empty =
      Result<std::optional<ClassicCanFrame>>::success(std::nullopt);
  CHECK("CAN-SOCKET-METADATA-003", empty.ok());
  CHECK("CAN-SOCKET-METADATA-003", !empty.value().has_value());
}

/** Verify deterministic interface validation and lookup failures. */
void test_open_failures() {
  const auto empty = CanSocket::open("");
  CHECK("CAN-SOCKET-001", !empty.ok());
  CHECK("CAN-SOCKET-001",
        empty.status().operation == "validate_interface_name");
  CHECK("CAN-SOCKET-001", empty.status().context == "interface=");
  CHECK("CAN-SOCKET-001", empty.status().error.value() == EINVAL);

  const std::string oversized(IFNAMSIZ, 'x');
  const auto too_long = CanSocket::open(oversized);
  CHECK("CAN-SOCKET-002", !too_long.ok());
  CHECK("CAN-SOCKET-002",
        too_long.status().operation == "validate_interface_name");
  CHECK("CAN-SOCKET-002",
        too_long.status().context == "interface=" + oversized);
  CHECK("CAN-SOCKET-002", too_long.status().error.value() == ENAMETOOLONG);

  const std::string missing_name = "missing/can0";
  errno = 0;
  CHECK("CAN-SOCKET-003-setup", ::if_nametoindex(missing_name.c_str()) == 0U);
  const int expected_errno = errno;
  const auto missing = CanSocket::open(missing_name);
  CHECK("CAN-SOCKET-003", !missing.ok());
  CHECK("CAN-SOCKET-003", missing.status().operation == "if_nametoindex");
  CHECK("CAN-SOCKET-003", missing.status().context == "interface=missing/can0");
  CHECK("CAN-SOCKET-003", expected_errno != 0);
  CHECK("CAN-SOCKET-003", missing.status().error.value() == expected_errno);
}

/** Verify closed-owner move operations without requiring a CAN interface. */
void test_closed_move_semantics() {
  CanSocket source;
  CanSocket moved{std::move(source)};
  CHECK("CAN-SOCKET-004", moved.fd() == -1);
  // CanSocket documents a closed, callable moved-from state.
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  const auto moved_from_receive = source.receive(0ms);
  CHECK("CAN-SOCKET-004", !moved_from_receive.ok());
  CHECK("CAN-SOCKET-004", moved_from_receive.status().error.value() == EBADF);

  CanSocket assigned;
  assigned = std::move(moved);
  CHECK("CAN-SOCKET-004", assigned.fd() == -1);
}

/** Verify deterministic frame-I/O validation without a CAN interface. */
void test_io_failures_without_interface() {
  CanSocket closed;
  const auto receive_closed = closed.receive(0ms);
  CHECK("CAN-SOCKET-IO-001", !receive_closed.ok());
  CHECK("CAN-SOCKET-IO-001", receive_closed.status().operation == "receive");
  CHECK("CAN-SOCKET-IO-001", receive_closed.status().context == "interface=");
  CHECK("CAN-SOCKET-IO-001", receive_closed.status().error.value() == EBADF);

  const ClassicCanFrame frame = test_frame();
  const auto send_closed = closed.send(frame, 0ms);
  CHECK("CAN-SOCKET-IO-002", !send_closed.ok());
  CHECK("CAN-SOCKET-IO-002", send_closed.operation == "send");
  CHECK("CAN-SOCKET-IO-002", send_closed.context == "interface=");
  CHECK("CAN-SOCKET-IO-002", send_closed.error.value() == EBADF);

  ClassicCanFrame invalid = frame;
  invalid.raw_can_id = CAN_ERR_FLAG | 1U;
  const auto invalid_send = closed.send(invalid, 0ms);
  CHECK("CAN-SOCKET-IO-003", !invalid_send.ok());
  CHECK("CAN-SOCKET-IO-003", invalid_send.operation == "validate_can_tx");
  CHECK("CAN-SOCKET-IO-003",
        invalid_send.context.starts_with("interface= raw_can_id="));
  CHECK("CAN-SOCKET-IO-003", invalid_send.error.value() == EINVAL);

  const auto negative_receive = closed.receive(-1ms);
  CHECK("CAN-SOCKET-IO-004", !negative_receive.ok());
  CHECK("CAN-SOCKET-IO-004", negative_receive.status().operation == "receive");
  CHECK("CAN-SOCKET-IO-004",
        negative_receive.status().context == "interface= negative timeout");
  CHECK("CAN-SOCKET-IO-004", negative_receive.status().error.value() == EINVAL);

  const auto negative_send = closed.send(frame, -1ms);
  CHECK("CAN-SOCKET-IO-005", !negative_send.ok());
  CHECK("CAN-SOCKET-IO-005", negative_send.operation == "send");
  CHECK("CAN-SOCKET-IO-005",
        negative_send.context == "interface= negative timeout");
  CHECK("CAN-SOCKET-IO-005", negative_send.error.value() == EINVAL);

  constexpr auto excessive_timeout = std::chrono::milliseconds::max();
  const auto overflow_receive = closed.receive(excessive_timeout);
  CHECK("CAN-SOCKET-IO-006", !overflow_receive.ok());
  CHECK("CAN-SOCKET-IO-006", overflow_receive.status().operation == "receive");
  CHECK("CAN-SOCKET-IO-006",
        overflow_receive.status().context == "interface= timeout overflow");
  CHECK("CAN-SOCKET-IO-006",
        overflow_receive.status().error.value() == EOVERFLOW);

  const auto overflow_send = closed.send(frame, excessive_timeout);
  CHECK("CAN-SOCKET-IO-007", !overflow_send.ok());
  CHECK("CAN-SOCKET-IO-007", overflow_send.operation == "send");
  CHECK("CAN-SOCKET-IO-007",
        overflow_send.context == "interface= timeout overflow");
  CHECK("CAN-SOCKET-IO-007", overflow_send.error.value() == EOVERFLOW);
}

/** Verify bind, flags, filter modes, error mask, moves, and automatic close. */
void test_configured_vcan() {
  const char *interface_name = std::getenv("ROBOT_CONTROL_TEST_VCAN_INTERFACE");
  if (interface_name == nullptr || interface_name[0] == '\0') {
    std::cout << "SKIP: set ROBOT_CONTROL_TEST_VCAN_INTERFACE to an existing "
                 "vcan interface for bind/filter/frame/metadata checks\n";
    return;
  }
  if (!std::string_view{interface_name}.starts_with("vcan")) {
    std::cout << "SKIP: ROBOT_CONTROL_TEST_VCAN_INTERFACE must identify a "
                 "vcan interface; refusing active CAN transmission\n";
    return;
  }

  const auto receive_all = CanSocket::open(interface_name);
  CHECK("CAN-SOCKET-005", receive_all.ok());
  if (!receive_all.ok()) {
    return;
  }
  CHECK("CAN-SOCKET-005",
        receive_all.value().interface_name() == interface_name);
  CHECK("CAN-SOCKET-005", receive_all.value().interface_index() != 0U);
  const int status_flags = ::fcntl(receive_all.value().fd(), F_GETFL);
  CHECK("CAN-SOCKET-005", status_flags >= 0);
  if (status_flags >= 0) {
    CHECK("CAN-SOCKET-005", (status_flags & O_NONBLOCK) != 0);
  }

  const int descriptor_flags = ::fcntl(receive_all.value().fd(), F_GETFD);
  CHECK("CAN-SOCKET-005", descriptor_flags >= 0);
  if (descriptor_flags >= 0) {
    CHECK("CAN-SOCKET-005", (descriptor_flags & FD_CLOEXEC) != 0);
  }

  const std::span<const ::can_filter> no_filters{};
  const auto receive_none = CanSocket::open(
      interface_name, CanSocketConfig{.filters = no_filters, .error_mask = 0});
  CHECK("CAN-SOCKET-006", receive_none.ok());

  const std::array<::can_filter, 1> filters{{
      {.can_id = 0x123U, .can_mask = CAN_SFF_MASK},
  }};
  auto filtered = CanSocket::open(
      interface_name,
      CanSocketConfig{.filters = std::span<const ::can_filter>{filters},
                      .error_mask = CAN_ERR_BUSOFF});
  CHECK("CAN-SOCKET-007", filtered.ok());
  if (!filtered.ok()) {
    return;
  }

  const auto test_can_id = static_cast<canid_t>(
      0x5a0U + (static_cast<unsigned int>(::getpid()) & 0x1fU));
  const std::array<::can_filter, 1> io_filters{{
      {.can_id = test_can_id,
       .can_mask = CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG},
  }};
  auto receive_socket = CanSocket::open(
      interface_name,
      CanSocketConfig{.filters = std::span<const ::can_filter>{io_filters},
                      .error_mask = 0,
                      .receive_timestamp = true,
                      .receive_queue_overflow = true});
  auto transmit_socket = CanSocket::open(
      interface_name, CanSocketConfig{.filters = no_filters, .error_mask = 0});
  CHECK("CAN-SOCKET-IO-008", receive_socket.ok());
  CHECK("CAN-SOCKET-IO-008", transmit_socket.ok());
  if (!receive_socket.ok() || !transmit_socket.ok()) {
    return;
  }

  for (int index = 0; index < 64; ++index) {
    const auto stale = receive_socket.value().receive(0ms);
    CHECK("CAN-SOCKET-IO-009", stale.ok());
    if (!stale.ok() || !stale.value().has_value()) {
      break;
    }
    CHECK("CAN-SOCKET-IO-009", index != 63);
  }
  const auto empty = receive_socket.value().receive(2ms);
  CHECK("CAN-SOCKET-IO-010", empty.ok());
  CHECK("CAN-SOCKET-IO-010", empty.ok() && !empty.value().has_value());

  std::array<int, 2> cancel_fds{};
  const bool cancel_ready =
      ::pipe2(cancel_fds.data(), O_CLOEXEC | O_NONBLOCK) == 0;
  CHECK("CAN-SOCKET-IO-011", cancel_ready);
  if (!cancel_ready) {
    return;
  }
  UniqueFd cancel_reader{cancel_fds[0]};
  UniqueFd cancel_writer{cancel_fds[1]};
  const std::byte cancel_value{0x1};
  CHECK("CAN-SOCKET-IO-011",
        ::write(cancel_writer.get(), &cancel_value, sizeof(cancel_value)) == 1);

  const auto cancelled_receive =
      receive_socket.value().receive(1s, cancel_reader.get());
  CHECK("CAN-SOCKET-IO-011", !cancelled_receive.ok());
  CHECK("CAN-SOCKET-IO-011", cancelled_receive.status().operation == "receive");
  CHECK("CAN-SOCKET-IO-011",
        cancelled_receive.status().error.value() == ECANCELED);
  std::byte observed_cancel{};
  CHECK("CAN-SOCKET-IO-011", ::read(cancel_reader.get(), &observed_cancel,
                                    sizeof(observed_cancel)) == 1);
  CHECK("CAN-SOCKET-IO-011", observed_cancel == cancel_value);
  CHECK("CAN-SOCKET-IO-011",
        ::write(cancel_writer.get(), &cancel_value, sizeof(cancel_value)) == 1);

  const ClassicCanFrame sent_frame = test_frame(test_can_id);
  const auto cancelled_send =
      transmit_socket.value().send(sent_frame, 1s, cancel_reader.get());
  CHECK("CAN-SOCKET-IO-012", !cancelled_send.ok());
  CHECK("CAN-SOCKET-IO-012", cancelled_send.operation == "send");
  CHECK("CAN-SOCKET-IO-012", cancelled_send.error.value() == ECANCELED);
  const auto not_sent = receive_socket.value().receive(2ms);
  CHECK("CAN-SOCKET-IO-012", not_sent.ok());
  CHECK("CAN-SOCKET-IO-012", not_sent.ok() && !not_sent.value().has_value());

  const auto sent = transmit_socket.value().send(sent_frame, 100ms);
  CHECK("CAN-SOCKET-IO-013", sent.ok());
  const auto received = receive_socket.value().receive(100ms);
  CHECK("CAN-SOCKET-IO-013", received.ok());
  CHECK("CAN-SOCKET-IO-013", received.ok() && received.value().has_value());
  if (received.ok() && received.value().has_value()) {
    const auto &observation = *received.value();
    const auto &frame = observation.frame;
    CHECK("CAN-SOCKET-IO-013", frame.raw_can_id == sent_frame.raw_can_id);
    CHECK("CAN-SOCKET-IO-013",
          frame.payload_length == sent_frame.payload_length);
    CHECK("CAN-SOCKET-IO-013", frame.len8_dlc == sent_frame.len8_dlc);
    CHECK("CAN-SOCKET-IO-013", frame.data == sent_frame.data);
    CHECK("CAN-SOCKET-METADATA-004", observation.kernel_timestamp.has_value());
    if (observation.kernel_timestamp.has_value()) {
      CHECK("CAN-SOCKET-METADATA-004",
            observation.kernel_timestamp->tv_nsec >= 0);
      CHECK("CAN-SOCKET-METADATA-004",
            observation.kernel_timestamp->tv_nsec < 1'000'000'000L);
    }
    if (!observation.rx_queue_overflow.has_value() ||
        *observation.rx_queue_overflow == 0U) {
      std::cout << "INFO: SO_RXQ_OVFL enabled; no nonzero overflow counter "
                   "was observed\n";
    }
  }

  CanSocket owned = std::move(filtered).value();
  const int descriptor = owned.fd();
  {
    CanSocket moved{std::move(owned)};
    CHECK("CAN-SOCKET-008", moved.fd() == descriptor);
    CanSocket assigned;
    assigned = std::move(moved);
    CHECK("CAN-SOCKET-008", assigned.fd() == descriptor);
  }
  errno = 0;
  CHECK("CAN-SOCKET-008", ::fcntl(descriptor, F_GETFD) == -1 && errno == EBADF);
}

} // namespace

/** Run policy-free SocketCAN lifecycle tests. */
int main() {
  test_metadata_config_and_nested_result();
  test_open_failures();
  test_closed_move_semantics();
  test_io_failures_without_interface();
  test_configured_vcan();
  if (failures != 0) {
    std::cerr << "socketcan_socket_tests failures=" << failures << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "socketcan_socket_tests passed\n";
  return EXIT_SUCCESS;
}
