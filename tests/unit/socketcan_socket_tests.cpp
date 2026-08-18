#include "platform/linux/can/socket.hpp"

#include <linux/can/error.h>

#include <fcntl.h>
#include <net/if.h>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using robot_control::platform::linux::can::CanSocket;
using robot_control::platform::linux::can::CanSocketConfig;

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

  CanSocket assigned;
  assigned = std::move(moved);
  CHECK("CAN-SOCKET-004", assigned.fd() == -1);
}

/** Verify bind, flags, filter modes, error mask, moves, and automatic close. */
void test_configured_vcan() {
  const char *interface_name = std::getenv("ROBOT_CONTROL_TEST_VCAN_INTERFACE");
  if (interface_name == nullptr || interface_name[0] == '\0') {
    std::cout << "SKIP: set ROBOT_CONTROL_TEST_VCAN_INTERFACE to an existing "
                 "vcan interface for bind/filter checks\n";
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
  test_open_failures();
  test_closed_move_semantics();
  test_configured_vcan();
  if (failures != 0) {
    std::cerr << "socketcan_socket_tests failures=" << failures << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "socketcan_socket_tests passed\n";
  return EXIT_SUCCESS;
}
