#include "communication/canopen/commissioning.hpp"
#include "platform/linux/can/socket.hpp"
#include "platform/linux/process/termination_event.hpp"

#include <linux/can.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <thread>

namespace {
using namespace std::chrono_literals;
using robot_control::communication::canopen::CommissioningNmt;
using robot_control::communication::canopen::CommissioningSession;
using robot_control::communication::canopen::Lifecycle;
using robot_control::communication::canopen::LifecycleExit;
using robot_control::communication::canopen::SdoOutcome;
using robot_control::communication::canopen::StackConfig;
using robot_control::platform::linux::can::CanSocket;
using robot_control::platform::linux::can::CanSocketConfig;
using robot_control::platform::linux::can::ClassicCanFrame;
using robot_control::platform::linux::process::TerminationEvent;

std::atomic<int> failures{0};
#define CHECK(expression)                                                                                              \
    do {                                                                                                               \
        if (!(expression)) {                                                                                           \
            ++failures;                                                                                                \
            std::cerr << #expression << " failed\n";                                                                   \
        }                                                                                                              \
    } while (false)

ClassicCanFrame frame(const canid_t identifier, const std::initializer_list<std::uint8_t> bytes) {
    ClassicCanFrame result{.raw_can_id = identifier, .payload_length = static_cast<std::uint8_t>(bytes.size())};
    std::transform(bytes.begin(), bytes.end(), result.data.begin(), [](const std::uint8_t value) {
        return std::byte{value};
    });
    return result;
}

bool same(const ClassicCanFrame& actual, const ClassicCanFrame& expected) {
    return actual.raw_can_id == expected.raw_can_id && actual.payload_length == expected.payload_length
           && actual.data == expected.data;
}

ClassicCanFrame receive(CanSocket& socket, const std::chrono::milliseconds timeout) {
    const auto result = socket.receive(timeout);
    CHECK(result.ok());
    CHECK(result.ok() && result.value().has_value());
    return result.ok() && result.value().has_value() ? result.value()->frame : ClassicCanFrame{};
}

void process(Lifecycle& owner, const std::chrono::milliseconds duration = 10ms) {
    const auto result = owner.run_until(std::chrono::steady_clock::now() + duration);
    CHECK(result.ok());
    CHECK(result.ok() && result.value() == LifecycleExit::deadline);
}

StackConfig config(const std::string_view interface_name) {
    return {.interface_name = std::string{interface_name},
            .controller_node_id = 127U,
            .remote_node_id = 1U,
            .bit_rate_kbit_s = 500U,
            .heartbeat_timeout = 2000ms,
            .sdo_timeout = 30ms,
            .tpdo_timeout = 100ms,
            .tpdo_expected_dlc = {8U, 0U, 0U, 0U}};
}

ClassicCanFrame upload_request(const std::uint16_t index, const std::uint8_t subindex) {
    return frame(0x601U, {0x40U, static_cast<std::uint8_t>(index), static_cast<std::uint8_t>(index >> 8U), subindex, 0U,
                          0U, 0U, 0U});
}

bool monitor_allowed(const ClassicCanFrame& value) {
    if (value.raw_can_id == 0x701U) {
        return value.payload_length == 1U;
    }
    if (value.raw_can_id == 0U) {
        const auto command = std::to_integer<std::uint8_t>(value.data[0]);
        return value.payload_length == 2U && (command == 0x02U || command == 0x80U) && value.data[1] == std::byte{1U};
    }
    return value.raw_can_id == 0x601U || value.raw_can_id == 0x581U;
}
} // namespace

int main() {
    const char* const interface_name = std::getenv("ROBOT_CONTROL_TEST_VCAN_INTERFACE");
    if (interface_name == nullptr || !std::string_view{interface_name}.starts_with("vcan")) {
        std::cout << "SKIP: P5.6 requires the isolated managed-vcan runner\n";
        return 77;
    }
    auto termination = TerminationEvent::create();
    CHECK(termination.ok());
    const std::array filters{can_filter{.can_id = 0x000U, .can_mask = CAN_SFF_MASK},
                             can_filter{.can_id = 0x601U, .can_mask = CAN_SFF_MASK}};
    auto peer_result =
        CanSocket::open(interface_name, CanSocketConfig{.filters = std::span<const can_filter>{filters}});
    auto monitor_result = CanSocket::open(interface_name, CanSocketConfig{});
    CHECK(peer_result.ok());
    CHECK(monitor_result.ok());
    if (!termination.ok() || !peer_result.ok() || !monitor_result.ok()) {
        return 1;
    }
    auto peer = std::move(peer_result).value();
    auto monitor = std::move(monitor_result).value();
    auto owner_result = Lifecycle::create(config(interface_name), termination.value());
    CHECK(owner_result.ok());
    if (!owner_result.ok()) {
        return 1;
    }
    auto owner = std::move(owner_result).value();
    CHECK(peer.send(frame(0x701U, {0x00U}), 100ms).ok());
    process(*owner);
    CHECK(peer.send(frame(0x701U, {0x7FU}), 100ms).ok());
    process(*owner);
    CommissioningSession session{*owner};

    CHECK(session.send_nmt(CommissioningNmt::stopped).ok());
    CHECK(same(receive(peer, 100ms), frame(0U, {0x02U, 1U})));
    CHECK(session.send_nmt(CommissioningNmt::pre_operational).ok());
    CHECK(same(receive(peer, 100ms), frame(0U, {0x80U, 1U})));

    std::jthread success_peer{[&] {
        CHECK(same(receive(peer, 200ms), upload_request(0x1001U, 0U)));
        CHECK(peer.send(frame(0x581U, {0x4FU, 0x01U, 0x10U, 0U, 0x5AU, 0U, 0U, 0U}), 100ms).ok());
    }};
    const auto upload = session.upload(0x1001U, 0U, false);
    success_peer.join();
    CHECK(upload.ok());
    CHECK(upload.ok() && upload.value().outcome == SdoOutcome::expedited_upload);
    CHECK(upload.ok() && upload.value().data_length == 1U && upload.value().data[0] == 0x5AU);

    const auto before_duplicate = owner->observation_snapshot(std::chrono::steady_clock::now()).sdo_rejection_count;
    CHECK(peer.send(frame(0x581U, {0x4FU, 0x01U, 0x10U, 0U, 0x5AU, 0U, 0U, 0U}), 100ms).ok());
    process(*owner);
    CHECK(owner->observation_snapshot(std::chrono::steady_clock::now()).sdo_rejection_count == before_duplicate + 1U);

    std::jthread abort_peer{[&] {
        CHECK(same(receive(peer, 200ms), upload_request(0x1000U, 0U)));
        CHECK(peer.send(frame(0x581U, {0x80U, 0x00U, 0x10U, 0U, 0x00U, 0x00U, 0x02U, 0x06U}), 100ms).ok());
    }};
    const auto aborted = session.upload(0x1000U, 0U, false);
    abort_peer.join();
    CHECK(aborted.ok());
    CHECK(aborted.ok() && aborted.value().outcome == SdoOutcome::abort);
    CHECK(aborted.ok() && aborted.value().abort_code == 0x06020000U);

    const auto before_late = owner->observation_snapshot(std::chrono::steady_clock::now()).sdo_rejection_count;
    std::jthread retry_peer{[&] {
        CHECK(same(receive(peer, 200ms), upload_request(0x2035U, 0U)));
        std::this_thread::sleep_for(40ms);
        CHECK(peer.send(frame(0x581U, {0x4BU, 0x35U, 0x20U, 0U, 0x34U, 0x12U, 0U, 0U}), 100ms).ok());
        CHECK(same(receive(peer, 200ms), upload_request(0x2035U, 0U)));
        CHECK(peer.send(frame(0x581U, {0x4BU, 0x35U, 0x20U, 0U, 0x78U, 0x56U, 0U, 0U}), 100ms).ok());
    }};
    const auto retried = session.upload(0x2035U, 0U, true);
    retry_peer.join();
    CHECK(retried.ok());
    CHECK(retried.ok() && retried.value().attempt_generation > upload.value().attempt_generation);
    CHECK(retried.ok() && retried.value().data[0] == 0x78U && retried.value().data[1] == 0x56U);
    CHECK(owner->observation_snapshot(std::chrono::steady_clock::now()).sdo_rejection_count >= before_late + 1U);

    std::size_t monitored = 0U;
    while (true) {
        const auto captured = monitor.receive(0ms);
        CHECK(captured.ok());
        if (!captured.ok() || !captured.value().has_value()) {
            break;
        }
        ++monitored;
        CHECK(monitor_allowed(captured.value()->frame));
    }
    CHECK(monitored >= 13U);
    std::cout << "INFO: P5.6 managed-vcan frames=" << monitored << " prohibited=0\n";
    return failures.load() == 0 ? 0 : 1;
}
