#include "communication/canopen/lifecycle.hpp"
#include "platform/linux/can/socket.hpp"
#include "platform/linux/process/termination_event.hpp"
#include "platform/linux/unique_fd.hpp"

#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <linux/if.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

namespace {

using namespace std::chrono_literals;
using robot_control::communication::canopen::FrameObservation;
using robot_control::communication::canopen::Lifecycle;
using robot_control::communication::canopen::LifecycleExit;
using robot_control::communication::canopen::ObservationGeneration;
using robot_control::communication::canopen::ObservationSnapshot;
using robot_control::communication::canopen::RemoteNmtState;
using robot_control::communication::canopen::StackConfig;
using robot_control::platform::linux::Status;
using robot_control::platform::linux::UniqueFd;
using robot_control::platform::linux::can::CanReceiveObservation;
using robot_control::platform::linux::can::CanSocket;
using robot_control::platform::linux::can::CanSocketConfig;
using robot_control::platform::linux::can::ClassicCanFrame;
using robot_control::platform::linux::process::TerminationEvent;

constexpr auto heartbeat_timeout = 300ms;
constexpr auto tpdo_timeout = 150ms;
int failures = 0;
std::size_t stimulus_count = 0U;

/** Record one managed-vcan assertion failure. */
void check(const bool condition, const std::string_view id, const std::string_view expression) {
    if (!condition) {
        ++failures;
        std::cerr << id << " failed: " << expression << '\n';
    }
}

#define CHECK(id, expression) check((expression), (id), #expression)

/** Print one context-rich status when an operation failed. */
void report_status(const std::string_view id, const Status& status) {
    std::cerr << id << " status: operation=" << status.operation << " context=\"" << status.context
              << "\" errno=" << status.error.value() << '\n';
}

/** Return the fixed non-actuating CANopen observer configuration. */
StackConfig test_config(const std::string& interface_name) {
    return StackConfig{
        .interface_name = interface_name,
        .controller_node_id = 127U,
        .remote_node_id = 1U,
        .bit_rate_kbit_s = 500U,
        .heartbeat_timeout = heartbeat_timeout,
        .sdo_timeout = 100ms,
        .tpdo_timeout = tpdo_timeout,
        .tpdo_expected_dlc = {8U, 0U, 0U, 0U},
    };
}

/** Build one deterministic Classical CAN stimulus frame. */
ClassicCanFrame frame(const canid_t identifier, const std::uint8_t dlc,
                      const std::initializer_list<std::uint8_t> payload) {
    ClassicCanFrame result{
        .raw_can_id = identifier,
        .payload_length = dlc,
    };
    std::transform(payload.begin(), payload.end(), result.data.begin(), [](const std::uint8_t value) {
        return std::byte{value};
    });
    return result;
}

/** Return whether an observation preserves the complete injected frame. */
bool same_raw_frame(const FrameObservation& observation, const ClassicCanFrame& expected) {
    if (observation.raw.identifier != expected.raw_can_id || observation.raw.dlc != expected.payload_length) {
        return false;
    }
    return std::equal(observation.raw.payload.begin(), observation.raw.payload.end(), expected.data.begin(),
                      [](const std::uint8_t actual, const std::byte wanted) {
                          return actual == std::to_integer<std::uint8_t>(wanted);
                      });
}

/** Print the raw observation retained by the production owner. */
void print_observation(const std::string_view service, const FrameObservation& observation) {
    const auto timestamp_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(observation.raw.received_at.time_since_epoch()).count();
    std::cout << "INFO: P5.5 observation service=" << service << " raw_can_id=0x" << std::hex << std::setfill('0')
              << std::setw(8) << observation.raw.identifier << std::dec << std::setfill(' ')
              << " dlc=" << static_cast<unsigned int>(observation.raw.dlc) << " payload=";
    for (const std::uint8_t value : observation.raw.payload) {
        std::cout << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned int>(value);
    }
    std::cout << std::dec << std::setfill(' ') << " timestamp_ns=" << timestamp_ns
              << " transport=" << observation.raw.generation.transport << " boot=" << observation.raw.generation.boot
              << '\n';
}

/** Verify one frame received by the independent zero-transmit monitor. */
void check_monitor_frame(CanSocket& monitor, const ClassicCanFrame& expected, const std::string_view id) {
    const auto received = monitor.receive(100ms);
    CHECK(id, received.ok());
    if (!received.ok()) {
        report_status(id, received.status());
        return;
    }
    CHECK(id, received.value().has_value());
    if (!received.value().has_value()) {
        return;
    }
    const CanReceiveObservation& actual = *received.value();
    CHECK(id, actual.frame.raw_can_id == expected.raw_can_id);
    CHECK(id, actual.frame.payload_length == expected.payload_length);
    CHECK(id, actual.frame.data == expected.data);
    ++stimulus_count;
}

/** Verify that normal observer processing emitted no additional CAN frame. */
void check_monitor_quiet(CanSocket& monitor, const std::string_view id, const std::chrono::milliseconds timeout = 5ms) {
    const auto received = monitor.receive(timeout);
    CHECK(id, received.ok());
    if (!received.ok()) {
        report_status(id, received.status());
        return;
    }
    CHECK(id, !received.value().has_value());
}

/** Run the production owner to one bounded deadline. */
bool run_to_deadline(Lifecycle& owner, const std::chrono::steady_clock::time_point deadline,
                     const std::string_view id) {
    const auto result = owner.run_until(deadline);
    CHECK(id, result.ok());
    if (!result.ok()) {
        report_status(id, result.status());
        return false;
    }
    CHECK(id, result.value() == LifecycleExit::deadline);
    return result.value() == LifecycleExit::deadline;
}

/** Send one peer frame and prove one production-owner consumption. */
ObservationSnapshot send_and_process(Lifecycle& owner, CanSocket& peer, const ClassicCanFrame& stimulus,
                                     CanSocket& monitor, const std::string_view id) {
    const auto before = owner.observation_snapshot(std::chrono::steady_clock::now());
    const auto sent = peer.send(stimulus, 100ms);
    CHECK(id, sent.ok());
    if (!sent.ok()) {
        report_status(id, sent);
        return before;
    }
    static_cast<void>(run_to_deadline(owner, std::chrono::steady_clock::now() + 10ms, id));
    check_monitor_frame(monitor, stimulus, id);
    check_monitor_quiet(monitor, id, 0ms);
    const auto after = owner.observation_snapshot(std::chrono::steady_clock::now());
    CHECK(id, after.version == before.version + 1U);
    return after;
}

/** Change one isolated test interface's administrative state. */
bool set_interface_up(const std::string_view interface_name, const bool requested_up) {
    if (interface_name.empty() || interface_name.size() >= IFNAMSIZ) {
        CHECK("P55-LINK-001", false);
        return false;
    }

    const int descriptor = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    CHECK("P55-LINK-001", descriptor >= 0);
    if (descriptor < 0) {
        return false;
    }
    UniqueFd control{descriptor};
    ifreq request{};
    interface_name.copy(request.ifr_name, interface_name.size());
    request.ifr_name[interface_name.size()] = '\0';
    if (::ioctl(control.get(), SIOCGIFFLAGS, &request) != 0) {
        CHECK("P55-LINK-001", false);
        return false;
    }
    request.ifr_flags =
        requested_up ? static_cast<short>(request.ifr_flags | IFF_UP) : static_cast<short>(request.ifr_flags & ~IFF_UP);
    if (::ioctl(control.get(), SIOCSIFFLAGS, &request) != 0) {
        CHECK("P55-LINK-001", false);
        return false;
    }
    if (::ioctl(control.get(), SIOCGIFFLAGS, &request) != 0) {
        CHECK("P55-LINK-001", false);
        return false;
    }
    return ((request.ifr_flags & IFF_UP) != 0) == requested_up;
}

/** Inject one bounded test-only CAN error frame on the isolated vcan bus. */
bool inject_error_frame(const unsigned int interface_index, const ClassicCanFrame& stimulus) {
    const int initial_failures = failures;
    const int descriptor = ::socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, CAN_RAW);
    CHECK("P55-ERROR-001", descriptor >= 0);
    if (descriptor < 0) {
        return false;
    }
    UniqueFd injector{descriptor};
    CHECK("P55-ERROR-001", ::setsockopt(injector.get(), SOL_CAN_RAW, CAN_RAW_FILTER, nullptr, 0) == 0);
    if (failures != initial_failures) {
        return false;
    }

    sockaddr_can address{};
    address.can_family = AF_CAN;
    address.can_ifindex = static_cast<int>(interface_index);
    CHECK("P55-ERROR-001",
          ::bind(injector.get(), reinterpret_cast<const sockaddr*>(&address), static_cast<socklen_t>(sizeof(address)))
              == 0);
    if (failures != initial_failures) {
        return false;
    }

    can_frame raw{};
    raw.can_id = stimulus.raw_can_id;
    raw.len = stimulus.payload_length;
    std::transform(stimulus.data.begin(), stimulus.data.end(), raw.data, [](const std::byte value) {
        return std::to_integer<std::uint8_t>(value);
    });
    const auto written = ::write(injector.get(), &raw, CAN_MTU);
    CHECK("P55-ERROR-001", written == static_cast<ssize_t>(CAN_MTU));
    return written == static_cast<ssize_t>(CAN_MTU);
}

/** Verify one expected signal exit while the CAN endpoint remains open. */
void check_signal_exit(Lifecycle& owner, CanSocket& monitor, const int signal, const LifecycleExit expected,
                       const std::string_view id) {
    CHECK(id, ::kill(::getpid(), signal) == 0);
    const auto result = owner.run_until(std::chrono::steady_clock::now() + 100ms);
    CHECK(id, result.ok());
    if (!result.ok()) {
        report_status(id, result.status());
        return;
    }
    CHECK(id, result.value() == expected);
    check_monitor_quiet(monitor, id, 20ms);
}

} // namespace

/** Run P5.5 production-owner evidence inside the managed vcan namespace. */
int main() {
    const char* const interface_name = std::getenv("ROBOT_CONTROL_TEST_VCAN_INTERFACE");
    const char* const allow_link_toggle = std::getenv("ROBOT_CONTROL_TEST_ALLOW_VCAN_LINK_TOGGLE");
    if (interface_name == nullptr || !std::string_view{interface_name}.starts_with("vcan")
        || allow_link_toggle == nullptr || std::string_view{allow_link_toggle} != "1") {
        std::cout << "SKIP: P5.5 requires the isolated managed-vcan runner\n";
        return 77;
    }

    auto termination_result = TerminationEvent::create();
    CHECK("P55-START-001", termination_result.ok());
    if (!termination_result.ok()) {
        report_status("P55-START-001", termination_result.status());
        return 1;
    }
    auto termination = std::move(termination_result).value();

    const std::span<const can_filter> no_filters{};
    auto peer_result = CanSocket::open(interface_name, CanSocketConfig{.filters = no_filters, .error_mask = 0});
    auto monitor_result = CanSocket::open(interface_name, CanSocketConfig{.error_mask = CAN_ERR_MASK});
    CHECK("P55-START-002", peer_result.ok());
    CHECK("P55-START-002", monitor_result.ok());
    if (!peer_result.ok() || !monitor_result.ok()) {
        return 1;
    }
    CanSocket peer = std::move(peer_result).value();
    CanSocket monitor = std::move(monitor_result).value();

    auto owner_result = Lifecycle::create(test_config(interface_name), termination);
    CHECK("P55-START-003", owner_result.ok());
    if (!owner_result.ok()) {
        report_status("P55-START-003", owner_result.status());
        return 1;
    }
    auto owner = std::move(owner_result).value();
    const auto initial = owner->observation_snapshot(std::chrono::steady_clock::now());
    CHECK("P55-START-004", initial.generation == (ObservationGeneration{1U, 0U}));
    CHECK("P55-START-004", initial.version == 1U);
    static_cast<void>(run_to_deadline(*owner, std::chrono::steady_clock::now() + 30ms, "P55-START-005"));
    check_monitor_quiet(monitor, "P55-ZERO-TX-001", 20ms);

    const auto boot = frame(0x701U, 1U, {0x00U});
    const auto heartbeat = frame(0x701U, 1U, {0x05U});
    const auto emergency = frame(0x081U, 8U, {0x34U, 0x12U, 0x56U, 0x78U, 0xefU, 0xcdU, 0xabU, 0x90U});
    const std::array<ClassicCanFrame, 4> tpdo{
        frame(0x181U, 8U, {0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U}), frame(0x281U, 0U, {}),
        frame(0x381U, 0U, {}), frame(0x481U, 0U, {})};

    auto snapshot = send_and_process(*owner, peer, boot, monitor, "P55-BOOT-001");
    CHECK("P55-BOOT-002", snapshot.boot_observed);
    CHECK("P55-BOOT-002", snapshot.generation == (ObservationGeneration{1U, 1U}));
    CHECK("P55-BOOT-002", same_raw_frame(snapshot.boot, boot));
    print_observation("boot", snapshot.boot);

    snapshot = send_and_process(*owner, peer, heartbeat, monitor, "P55-HEARTBEAT-001");
    CHECK("P55-HEARTBEAT-002", snapshot.nmt.current);
    CHECK("P55-HEARTBEAT-002", snapshot.nmt.state == RemoteNmtState::operational);
    CHECK("P55-HEARTBEAT-002", same_raw_frame(snapshot.heartbeat.frame, heartbeat));
    print_observation("heartbeat", snapshot.heartbeat.frame);

    snapshot = send_and_process(*owner, peer, emergency, monitor, "P55-EMCY-001");
    CHECK("P55-EMCY-002", same_raw_frame(snapshot.emergency.frame, emergency));
    CHECK("P55-EMCY-002", snapshot.emergency.error_code == 0x1234U);
    CHECK("P55-EMCY-002", snapshot.emergency.error_register == 0x56U);
    CHECK("P55-EMCY-002", snapshot.emergency.error_bit == 0x78U);
    CHECK("P55-EMCY-002", snapshot.emergency.info_code == 0x90abcdefU);
    print_observation("emcy", snapshot.emergency.frame);

    for (std::size_t index = 0U; index < tpdo.size(); ++index) {
        snapshot = send_and_process(*owner, peer, tpdo[index], monitor, "P55-TPDO-001");
        CHECK("P55-TPDO-002", snapshot.tpdo[index].current);
        CHECK("P55-TPDO-002", same_raw_frame(snapshot.tpdo[index], tpdo[index]));
        print_observation(std::string{"tpdo"} + std::to_string(index + 1U), snapshot.tpdo[index]);
    }
    CHECK("P55-SDO-001", !snapshot.sdo_result.present);

    CHECK("P55-ORDER-001", snapshot.boot.raw.received_at <= snapshot.heartbeat.frame.raw.received_at);
    CHECK("P55-ORDER-001", snapshot.heartbeat.frame.raw.received_at <= snapshot.emergency.frame.raw.received_at);
    for (std::size_t index = 0U; index < snapshot.tpdo.size(); ++index) {
        const auto at_boundary = owner->observation_snapshot(snapshot.tpdo[index].raw.received_at + tpdo_timeout);
        const auto after_boundary =
            owner->observation_snapshot(snapshot.tpdo[index].raw.received_at + tpdo_timeout + 1ns);
        CHECK("P55-TPDO-TIMEOUT-001", at_boundary.tpdo[index].current);
        CHECK("P55-TPDO-TIMEOUT-002", !after_boundary.tpdo[index].current);
    }
    const auto heartbeat_at_boundary =
        owner->observation_snapshot(snapshot.heartbeat.frame.raw.received_at + heartbeat_timeout);
    const auto heartbeat_after_boundary =
        owner->observation_snapshot(snapshot.heartbeat.frame.raw.received_at + heartbeat_timeout + 1ns);
    CHECK("P55-HEARTBEAT-TIMEOUT-001", heartbeat_at_boundary.heartbeat.frame.current);
    CHECK("P55-HEARTBEAT-TIMEOUT-002", !heartbeat_after_boundary.heartbeat.frame.current);

    const auto before_tpdo_timeout_version = snapshot.version;
    const auto last_tpdo_deadline = snapshot.tpdo.back().raw.received_at + tpdo_timeout + 5ms;
    static_cast<void>(run_to_deadline(*owner, last_tpdo_deadline, "P55-TPDO-TIMEOUT-003"));
    snapshot = owner->observation_snapshot(std::chrono::steady_clock::now());
    CHECK("P55-TPDO-TIMEOUT-004", std::ranges::none_of(snapshot.tpdo, [](const FrameObservation& value) {
              return value.current;
          }));
    CHECK("P55-TPDO-TIMEOUT-004", snapshot.heartbeat.frame.current);
    CHECK("P55-TPDO-TIMEOUT-004", snapshot.version > before_tpdo_timeout_version);

    const auto before_heartbeat_timeout_version = snapshot.version;
    static_cast<void>(run_to_deadline(*owner, snapshot.heartbeat.frame.raw.received_at + heartbeat_timeout + 5ms,
                                      "P55-HEARTBEAT-TIMEOUT-003"));
    snapshot = owner->observation_snapshot(std::chrono::steady_clock::now());
    CHECK("P55-HEARTBEAT-TIMEOUT-004", !snapshot.heartbeat.frame.current);
    CHECK("P55-HEARTBEAT-TIMEOUT-004", !snapshot.nmt.current);
    CHECK("P55-HEARTBEAT-TIMEOUT-004", snapshot.version > before_heartbeat_timeout_version);

    const auto error = frame(CAN_ERR_FLAG | CAN_ERR_BUSOFF | CAN_ERR_CRTL, CAN_ERR_DLC,
                             {0x00U, CAN_ERR_CRTL_RX_WARNING, 0x12U, 0x34U, 0x56U, 0x78U, 0x9aU, 0xbcU});
    const auto before_error = snapshot;
    CHECK("P55-ERROR-001", inject_error_frame(monitor.interface_index(), error));
    static_cast<void>(run_to_deadline(*owner, std::chrono::steady_clock::now() + 10ms, "P55-ERROR-002"));
    check_monitor_frame(monitor, error, "P55-ERROR-003");
    check_monitor_quiet(monitor, "P55-ZERO-TX-002", 0ms);
    snapshot = owner->observation_snapshot(std::chrono::steady_clock::now());
    CHECK("P55-ERROR-004", snapshot.can_error.present);
    CHECK("P55-ERROR-004", same_raw_frame(snapshot.can_error, error));
    CHECK("P55-ERROR-004", snapshot.generation.transport == before_error.generation.transport + 1U);
    CHECK("P55-ERROR-004", snapshot.generation.boot == 0U);
    CHECK("P55-ERROR-004", !snapshot.boot_observed);
    print_observation("can_error", snapshot.can_error);

    snapshot = send_and_process(*owner, peer, heartbeat, monitor, "P55-ERROR-REBOOT-001");
    CHECK("P55-ERROR-REBOOT-002", !snapshot.heartbeat.frame.current);
    snapshot = send_and_process(*owner, peer, boot, monitor, "P55-ERROR-REBOOT-003");
    snapshot = send_and_process(*owner, peer, heartbeat, monitor, "P55-ERROR-REBOOT-004");
    CHECK("P55-ERROR-REBOOT-005", snapshot.heartbeat.frame.current);

    const auto generation_before_link_loss = snapshot.generation;
    CHECK("P55-LINK-002", set_interface_up(interface_name, false));
    const auto link_started_at = std::chrono::steady_clock::now();
    const auto link_result = owner->run_until(std::chrono::steady_clock::now() + 100ms);
    const auto link_elapsed = std::chrono::steady_clock::now() - link_started_at;
    CHECK("P55-LINK-003", !link_result.ok());
    if (!link_result.ok()) {
        CHECK("P55-LINK-003", link_result.status().operation == "ioctl(SIOCGIFFLAGS)");
        CHECK("P55-LINK-003", link_result.status().context.find(interface_name) != std::string::npos);
        CHECK("P55-LINK-003", link_result.status().error.value() == ENETDOWN);
        CHECK("P55-LINK-003", link_elapsed < 100ms);
        std::cout << "INFO: P5.5 link loss operation=" << link_result.status().operation << " context=\""
                  << link_result.status().context << "\" errno=" << link_result.status().error.value() << '\n';
    }
    snapshot = owner->observation_snapshot(std::chrono::steady_clock::now());
    CHECK("P55-LINK-004", !snapshot.boot_observed);
    CHECK("P55-LINK-004", !snapshot.heartbeat.frame.current);
    CHECK("P55-LINK-004", snapshot.generation.transport > generation_before_link_loss.transport);
    CHECK("P55-LINK-005", set_interface_up(interface_name, true));

    auto reopened_peer_result =
        CanSocket::open(interface_name, CanSocketConfig{.filters = no_filters, .error_mask = 0});
    auto reopened_monitor_result = CanSocket::open(interface_name, CanSocketConfig{.error_mask = CAN_ERR_MASK});
    CHECK("P55-LINK-006", reopened_peer_result.ok());
    CHECK("P55-LINK-006", reopened_monitor_result.ok());
    if (!reopened_peer_result.ok() || !reopened_monitor_result.ok()) {
        return 1;
    }
    peer = std::move(reopened_peer_result).value();
    monitor = std::move(reopened_monitor_result).value();
    const auto reopen_status = owner->reopen();
    CHECK("P55-LINK-007", reopen_status.ok());
    if (!reopen_status.ok()) {
        report_status("P55-LINK-007", reopen_status);
        return 1;
    }
    check_monitor_quiet(monitor, "P55-ZERO-TX-003", 20ms);
    snapshot = owner->observation_snapshot(std::chrono::steady_clock::now());
    CHECK("P55-LINK-008", snapshot.generation.boot == 0U);
    CHECK("P55-LINK-008", !snapshot.boot_observed);

    snapshot = send_and_process(*owner, peer, heartbeat, monitor, "P55-LINK-REBOOT-001");
    CHECK("P55-LINK-REBOOT-002", !snapshot.heartbeat.frame.current);
    snapshot = send_and_process(*owner, peer, boot, monitor, "P55-LINK-REBOOT-003");
    snapshot = send_and_process(*owner, peer, heartbeat, monitor, "P55-LINK-REBOOT-004");
    CHECK("P55-LINK-REBOOT-005", snapshot.heartbeat.frame.current);
    CHECK("P55-LINK-REBOOT-005", snapshot.generation.boot == 1U);

    check_signal_exit(*owner, monitor, SIGINT, LifecycleExit::sigint, "P55-SIGNAL-INT");
    check_signal_exit(*owner, monitor, SIGTERM, LifecycleExit::sigterm, "P55-SIGNAL-TERM");

    if (failures == 0) {
        std::cout << "INFO: P5.5 managed-vcan receive evidence passed"
                  << " interface=" << interface_name << " peer_frames=" << stimulus_count << " observer_tx_frames=0\n";
    }
    return failures == 0 ? 0 : 1;
}
