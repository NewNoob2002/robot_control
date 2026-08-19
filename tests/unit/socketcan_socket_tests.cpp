#include "platform/linux/can/socket.hpp"

#include <linux/can/error.h>
#include <linux/can/raw.h>

#include <fcntl.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
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
using robot_control::platform::linux::can::CanReceiveObservation;
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
      .payload_length = 8U,
      .len8_dlc = 0U,
      .data = {std::byte{0x11}, std::byte{0x22}, std::byte{0x33},
               std::byte{0x44}, std::byte{0x55}, std::byte{0x66},
               std::byte{0x77}, std::byte{0x88}},
  };
}

/** Drain queued frames before a managed-vcan assertion sequence. */
bool drain_socket(CanSocket &socket, const std::string_view id) {
  for (int index = 0; index < 64; ++index) {
    const auto stale = socket.receive(0ms);
    CHECK(id, stale.ok());
    if (!stale.ok()) {
      return false;
    }
    if (!stale.value().has_value()) {
      return true;
    }
  }
  CHECK(id, false);
  return false;
}

/** Verify one complete received observation against its transmitted frame. */
void check_received_frame(
    const std::string_view id,
    const Result<std::optional<CanReceiveObservation>> &received,
    const ClassicCanFrame &expected) {
  CHECK(id, received.ok());
  CHECK(id, received.ok() && received.value().has_value());
  if (!received.ok() || !received.value().has_value()) {
    return;
  }
  const auto &actual = received.value()->frame;
  CHECK(id, actual.raw_can_id == expected.raw_can_id);
  CHECK(id, actual.payload_length == expected.payload_length);
  CHECK(id, actual.len8_dlc == expected.len8_dlc);
  CHECK(id, actual.data == expected.data);
}

/** Verify that a filtered receiver observes no frame before its deadline. */
void check_receive_timeout(CanSocket &socket, const std::string_view id) {
  const auto received = socket.receive(20ms);
  CHECK(id, received.ok());
  CHECK(id, received.ok() && !received.value().has_value());
}

/** Return a bounded timeout that does not extend the supplied deadline. */
std::chrono::milliseconds
remaining_timeout(const std::chrono::steady_clock::time_point deadline,
                  const std::chrono::milliseconds maximum) {
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
      deadline - std::chrono::steady_clock::now());
  return std::clamp(remaining, 0ms, maximum);
}

/** Inject one test-only error frame through a bound raw SocketCAN socket. */
bool inject_test_error_frame(const unsigned int interface_index,
                             const ::can_frame &frame) {
  const int descriptor =
      ::socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, CAN_RAW);
  CHECK("CAN-SOCKET-ERROR-003", descriptor >= 0);
  if (descriptor < 0) {
    return false;
  }
  UniqueFd injector{descriptor};

  const int filter_result =
      ::setsockopt(injector.get(), SOL_CAN_RAW, CAN_RAW_FILTER, nullptr, 0);
  CHECK("CAN-SOCKET-ERROR-003", filter_result == 0);
  if (filter_result != 0) {
    return false;
  }

  sockaddr_can address{};
  address.can_family = AF_CAN;
  address.can_ifindex = static_cast<int>(interface_index);
  const int bind_result =
      ::bind(injector.get(), reinterpret_cast<const sockaddr *>(&address),
             static_cast<socklen_t>(sizeof(address)));
  CHECK("CAN-SOCKET-ERROR-003", bind_result == 0);
  if (bind_result != 0) {
    return false;
  }

  const ssize_t count = ::write(injector.get(), &frame, CAN_MTU);
  CHECK("CAN-SOCKET-ERROR-003", count == static_cast<ssize_t>(CAN_MTU));
  return count == static_cast<ssize_t>(CAN_MTU);
}

/** Verify raw CAN error-frame subscription and complete payload preservation.
 */
void test_error_frame_runtime(const char *interface_name) {
  constexpr can_err_mask_t error_classes = CAN_ERR_BUSOFF | CAN_ERR_CRTL;
  constexpr canid_t raw_can_id = CAN_ERR_FLAG | error_classes;
  constexpr std::array<std::byte, CAN_ERR_DLC> payload{
      std::byte{0x00}, std::byte{CAN_ERR_CRTL_RX_WARNING},
      std::byte{0x12}, std::byte{0x34},
      std::byte{0x56}, std::byte{0x78},
      std::byte{0x9a}, std::byte{0xbc}};
  const std::span<const ::can_filter> no_filters{};
  const int initial_failures = failures;
  auto receiver = CanSocket::open(
      interface_name,
      CanSocketConfig{.filters = no_filters, .error_mask = error_classes});
  auto control = CanSocket::open(
      interface_name, CanSocketConfig{.filters = no_filters, .error_mask = 0});
  CHECK("CAN-SOCKET-ERROR-001", receiver.ok());
  CHECK("CAN-SOCKET-ERROR-001", control.ok());
  if (!receiver.ok() || !control.ok()) {
    return;
  }
  if (!drain_socket(receiver.value(), "CAN-SOCKET-ERROR-002") ||
      !drain_socket(control.value(), "CAN-SOCKET-ERROR-002")) {
    return;
  }

  ::can_frame injected{};
  injected.can_id = raw_can_id;
  injected.len = CAN_ERR_DLC;
  injected.len8_dlc = 0U;
  std::copy(payload.begin(), payload.end(),
            reinterpret_cast<std::byte *>(injected.data));
  if (!inject_test_error_frame(receiver.value().interface_index(), injected)) {
    return;
  }

  const ClassicCanFrame expected{
      .raw_can_id = raw_can_id,
      .payload_length = CAN_ERR_DLC,
      .len8_dlc = 0U,
      .data = payload,
  };
  const auto received = receiver.value().receive(100ms);
  check_received_frame("CAN-SOCKET-ERROR-004", received, expected);
  check_receive_timeout(control.value(), "CAN-SOCKET-ERROR-005");

  if (failures == initial_failures) {
    std::cout << "INFO: managed vcan CAN error-frame evidence raw_can_id=0x"
              << std::hex << std::setfill('0') << std::setw(8) << raw_can_id
              << " error_classes=0x" << std::setw(8) << error_classes
              << " payload=";
    for (const std::byte value : payload) {
      std::cout << std::setw(2) << std::to_integer<unsigned int>(value);
    }
    std::cout << std::dec << std::setfill(' ') << '\n';
  }
}

/** Produce and verify a nonzero raw kernel RX queue overflow counter. */
void test_receive_queue_overflow(const char *interface_name,
                                 const canid_t test_can_id) {
  constexpr int requested_receive_buffer = 1024;
  constexpr std::size_t maximum_pressure_frames = 4096;
  constexpr std::size_t maximum_drain_frames = 256;
  const auto scenario_deadline = std::chrono::steady_clock::now() + 3s;
  const std::array<::can_filter, 1> filter{{
      {.can_id = test_can_id,
       .can_mask = CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG},
  }};
  const std::span<const ::can_filter> no_filters{};
  auto receiver = CanSocket::open(
      interface_name,
      CanSocketConfig{.filters = std::span<const ::can_filter>{filter},
                      .error_mask = 0,
                      .receive_queue_overflow = true});
  auto sender = CanSocket::open(
      interface_name, CanSocketConfig{.filters = no_filters, .error_mask = 0});
  CHECK("CAN-SOCKET-OVERFLOW-001", receiver.ok());
  CHECK("CAN-SOCKET-OVERFLOW-001", sender.ok());
  if (!receiver.ok() || !sender.ok()) {
    return;
  }

  const int set_buffer_result = ::setsockopt(
      receiver.value().fd(), SOL_SOCKET, SO_RCVBUF, &requested_receive_buffer,
      static_cast<socklen_t>(sizeof(requested_receive_buffer)));
  CHECK("CAN-SOCKET-OVERFLOW-002", set_buffer_result == 0);
  if (set_buffer_result != 0) {
    return;
  }

  int effective_receive_buffer = 0;
  socklen_t buffer_length =
      static_cast<socklen_t>(sizeof(effective_receive_buffer));
  const int get_buffer_result =
      ::getsockopt(receiver.value().fd(), SOL_SOCKET, SO_RCVBUF,
                   &effective_receive_buffer, &buffer_length);
  CHECK("CAN-SOCKET-OVERFLOW-003", get_buffer_result == 0);
  CHECK("CAN-SOCKET-OVERFLOW-003",
        buffer_length == sizeof(effective_receive_buffer));
  CHECK("CAN-SOCKET-OVERFLOW-003", effective_receive_buffer > 0);
  if (get_buffer_result != 0 ||
      buffer_length != sizeof(effective_receive_buffer) ||
      effective_receive_buffer <= 0) {
    return;
  }

  const ClassicCanFrame pressure_frame = test_frame(test_can_id);
  std::size_t frames_attempted = 0;
  std::size_t frames_sent = 0;
  while (frames_attempted < maximum_pressure_frames &&
         std::chrono::steady_clock::now() < scenario_deadline) {
    ++frames_attempted;
    const auto sent = sender.value().send(
        pressure_frame, remaining_timeout(scenario_deadline, 10ms));
    CHECK("CAN-SOCKET-OVERFLOW-004", sent.ok());
    if (!sent.ok()) {
      break;
    }
    ++frames_sent;
  }
  CHECK("CAN-SOCKET-OVERFLOW-004", frames_sent == maximum_pressure_frames);
  if (frames_sent != maximum_pressure_frames) {
    return;
  }

  bool drained = false;
  for (std::size_t drained_frames = 0;
       drained_frames < maximum_drain_frames &&
       std::chrono::steady_clock::now() < scenario_deadline;
       ++drained_frames) {
    const auto stale =
        receiver.value().receive(remaining_timeout(scenario_deadline, 1ms));
    CHECK("CAN-SOCKET-OVERFLOW-005", stale.ok());
    if (!stale.ok()) {
      return;
    }
    if (!stale.value().has_value()) {
      drained = true;
      break;
    }
  }
  CHECK("CAN-SOCKET-OVERFLOW-005", drained);
  if (!drained) {
    return;
  }

  ClassicCanFrame marker = test_frame(test_can_id);
  marker.data = {std::byte{0xde}, std::byte{0xad}, std::byte{0xbe},
                 std::byte{0xef}, std::byte{0x01}, std::byte{0x23},
                 std::byte{0x45}, std::byte{0x67}};
  const auto marker_sent =
      sender.value().send(marker, remaining_timeout(scenario_deadline, 100ms));
  CHECK("CAN-SOCKET-OVERFLOW-006", marker_sent.ok());
  if (!marker_sent.ok()) {
    return;
  }
  const auto marker_received =
      receiver.value().receive(remaining_timeout(scenario_deadline, 100ms));
  check_received_frame("CAN-SOCKET-OVERFLOW-006", marker_received, marker);
  if (!marker_received.ok() || !marker_received.value().has_value()) {
    return;
  }
  const auto overflow = marker_received.value()->rx_queue_overflow;
  CHECK("CAN-SOCKET-OVERFLOW-007", overflow.has_value());
  CHECK("CAN-SOCKET-OVERFLOW-007", overflow.value_or(0U) > 0U);
  if (!overflow.has_value() || *overflow == 0U) {
    return;
  }

  std::cout << "INFO: managed vcan RX overflow evidence requested_rcvbuf="
            << requested_receive_buffer
            << " effective_rcvbuf=" << effective_receive_buffer
            << " frames_attempted=" << frames_attempted
            << " frames_sent=" << frames_sent
            << " raw_overflow_counter=" << *overflow << '\n';
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
  const int initial_failures = failures;
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

  const auto test_can_id_a = static_cast<canid_t>(
      0x500U + ((static_cast<unsigned int>(::getpid()) & 0x3fU) * 2U));
  const auto test_can_id_b = static_cast<canid_t>(test_can_id_a + 1U);
  const std::array<::can_filter, 1> filter_a{{
      {.can_id = test_can_id_a,
       .can_mask = CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG},
  }};
  const std::array<::can_filter, 1> filter_b{{
      {.can_id = test_can_id_b,
       .can_mask = CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG},
  }};
  auto endpoint_a = CanSocket::open(
      interface_name,
      CanSocketConfig{.filters = std::span<const ::can_filter>{filter_b},
                      .error_mask = 0,
                      .receive_timestamp = true,
                      .receive_queue_overflow = true});
  auto endpoint_b = CanSocket::open(
      interface_name,
      CanSocketConfig{.filters = std::span<const ::can_filter>{filter_a},
                      .error_mask = 0,
                      .receive_timestamp = true,
                      .receive_queue_overflow = true});
  auto isolation_a = CanSocket::open(
      interface_name,
      CanSocketConfig{.filters = std::span<const ::can_filter>{filter_a},
                      .error_mask = 0});
  auto isolation_b = CanSocket::open(
      interface_name,
      CanSocketConfig{.filters = std::span<const ::can_filter>{filter_b},
                      .error_mask = 0});
  CHECK("CAN-SOCKET-IO-008", endpoint_a.ok());
  CHECK("CAN-SOCKET-IO-008", endpoint_b.ok());
  CHECK("CAN-SOCKET-IO-008", isolation_a.ok());
  CHECK("CAN-SOCKET-IO-008", isolation_b.ok());
  if (!endpoint_a.ok() || !endpoint_b.ok() || !isolation_a.ok() ||
      !isolation_b.ok()) {
    return;
  }

  if (!drain_socket(endpoint_a.value(), "CAN-SOCKET-IO-009") ||
      !drain_socket(endpoint_b.value(), "CAN-SOCKET-IO-009") ||
      !drain_socket(isolation_a.value(), "CAN-SOCKET-IO-009") ||
      !drain_socket(isolation_b.value(), "CAN-SOCKET-IO-009")) {
    return;
  }

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
      endpoint_a.value().receive(1s, cancel_reader.get());
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

  const ClassicCanFrame frame_a = test_frame(test_can_id_a);
  const auto cancelled_send =
      endpoint_a.value().send(frame_a, 1s, cancel_reader.get());
  CHECK("CAN-SOCKET-IO-012", !cancelled_send.ok());
  CHECK("CAN-SOCKET-IO-012", cancelled_send.operation == "send");
  CHECK("CAN-SOCKET-IO-012", cancelled_send.error.value() == ECANCELED);
  check_receive_timeout(endpoint_b.value(), "CAN-SOCKET-IO-012");
  check_receive_timeout(isolation_a.value(), "CAN-SOCKET-IO-012");

  const auto sent_a = endpoint_a.value().send(frame_a, 100ms);
  CHECK("CAN-SOCKET-IO-013", sent_a.ok());
  const auto received_by_b = endpoint_b.value().receive(100ms);
  check_received_frame("CAN-SOCKET-IO-013", received_by_b, frame_a);
  const auto isolated_a = isolation_a.value().receive(100ms);
  check_received_frame("CAN-SOCKET-FILTER-001", isolated_a, frame_a);
  check_receive_timeout(isolation_b.value(), "CAN-SOCKET-FILTER-001");
  if (received_by_b.ok() && received_by_b.value().has_value()) {
    const auto &observation = *received_by_b.value();
    CHECK("CAN-SOCKET-METADATA-004", observation.kernel_timestamp.has_value());
    if (observation.kernel_timestamp.has_value()) {
      CHECK("CAN-SOCKET-METADATA-004",
            observation.kernel_timestamp->tv_nsec >= 0);
      CHECK("CAN-SOCKET-METADATA-004",
            observation.kernel_timestamp->tv_nsec < 1'000'000'000L);
    }
  }

  ClassicCanFrame frame_b = test_frame(test_can_id_b);
  frame_b.data = {std::byte{0x88}, std::byte{0x77}, std::byte{0x66},
                  std::byte{0x55}, std::byte{0x44}, std::byte{0x33},
                  std::byte{0x22}, std::byte{0x11}};
  const auto sent_b = endpoint_b.value().send(frame_b, 100ms);
  CHECK("CAN-SOCKET-IO-014", sent_b.ok());
  const auto received_by_a = endpoint_a.value().receive(100ms);
  check_received_frame("CAN-SOCKET-IO-014", received_by_a, frame_b);
  const auto isolated_b = isolation_b.value().receive(100ms);
  check_received_frame("CAN-SOCKET-FILTER-002", isolated_b, frame_b);
  check_receive_timeout(isolation_a.value(), "CAN-SOCKET-FILTER-002");

  test_receive_queue_overflow(interface_name,
                              static_cast<canid_t>(test_can_id_a + 0x100U));
  test_error_frame_runtime(interface_name);

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
  if (failures == initial_failures) {
    std::cout << "INFO: managed vcan bidirectional frame and filter isolation "
                 "checks passed\n";
  }
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
