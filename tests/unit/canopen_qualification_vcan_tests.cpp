#include "communication/canopen/qualification.hpp"
#include "platform/linux/can/socket.hpp"
#include "platform/linux/process/termination_event.hpp"
#include "platform/linux/unique_fd.hpp"

#include <linux/can.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <thread>

namespace {

using namespace std::chrono_literals;
using robot_control::communication::canopen::CommunicationLossStimulus;
using robot_control::communication::canopen::Lifecycle;
using robot_control::communication::canopen::LifecycleExit;
using robot_control::communication::canopen::QualificationNmt;
using robot_control::communication::canopen::QualificationSession;
using robot_control::communication::canopen::QualificationState;
using robot_control::communication::canopen::StackConfig;
using robot_control::domain::drive::zlac8015d::IndependentChannel;
using robot_control::domain::drive::zlac8015d::TransitionControlword;
using robot_control::platform::linux::UniqueFd;
using robot_control::platform::linux::can::CanSocket;
using robot_control::platform::linux::can::CanSocketConfig;
using robot_control::platform::linux::can::ClassicCanFrame;
using robot_control::platform::linux::process::TerminationEvent;

std::atomic<int> failures{0};
#define CHECK(expression)                                                                                              \
    do {                                                                                                               \
        if (!(expression)) {                                                                                           \
            ++failures;                                                                                                \
            std::cerr << #expression << " failed line=" << __LINE__ << '\n';                                           \
        }                                                                                                              \
    } while (false)

/** Build one Classical CAN test frame. */
ClassicCanFrame frame(const canid_t identifier, const std::initializer_list<std::uint8_t> bytes) {
    ClassicCanFrame result{.raw_can_id = identifier, .payload_length = static_cast<std::uint8_t>(bytes.size())};
    std::transform(bytes.begin(), bytes.end(), result.data.begin(), [](const std::uint8_t value) {
        return std::byte{value};
    });
    return result;
}

/** Return whether two complete Classical CAN frames match. */
bool same(const ClassicCanFrame& actual, const ClassicCanFrame& expected) {
    return actual.raw_can_id == expected.raw_can_id && actual.payload_length == expected.payload_length
           && actual.data == expected.data;
}

/** Receive one required frame within a bounded wait. */
ClassicCanFrame receive(CanSocket& socket, const std::chrono::milliseconds timeout = 200ms) {
    const auto result = socket.receive(timeout);
    CHECK(result.ok());
    CHECK(result.ok() && result.value().has_value());
    return result.ok() && result.value().has_value() ? result.value()->frame : ClassicCanFrame{};
}

/** Require that no frame is queued before the bounded wait expires. */
void expect_no_frame(CanSocket& socket) {
    const auto result = socket.receive(20ms);
    CHECK(result.ok());
    CHECK(result.ok() && !result.value().has_value());
}

/** Return whether one captured frame belongs to the bounded P6.3 exchange. */
bool monitor_allowed(const ClassicCanFrame& value) {
    if (value.raw_can_id == 0x701U) {
        return value.payload_length == 1U;
    }
    if (value.raw_can_id == 0U) {
        const auto command = std::to_integer<std::uint8_t>(value.data[0]);
        return value.payload_length == 2U && (command == 0x01U || command == 0x02U || command == 0x80U)
               && value.data[1] == std::byte{1U};
    }
    return (value.raw_can_id == 0x081U || value.raw_can_id == 0x181U || value.raw_can_id == 0x601U
            || value.raw_can_id == 0x581U)
           && value.payload_length == 8U;
}

/** Process the sole CANopen owner for one bounded interval. */
void process(Lifecycle& owner, const std::chrono::milliseconds duration = 10ms) {
    const auto result = owner.run_until(std::chrono::steady_clock::now() + duration);
    CHECK(result.ok());
    CHECK(result.ok() && result.value() == LifecycleExit::deadline);
}

/** Build the fixed node-1 qualification test configuration. */
StackConfig config(const std::string_view interface_name, const std::chrono::milliseconds heartbeat = 2000ms,
                   const std::chrono::milliseconds tpdo = 100ms) {
    return {.interface_name = std::string{interface_name},
            .controller_node_id = 127U,
            .remote_node_id = 1U,
            .bit_rate_kbit_s = 500U,
            .heartbeat_timeout = heartbeat,
            .sdo_timeout = 30ms,
            .tpdo_timeout = tpdo,
            .tpdo_expected_dlc = {8U, 0U, 0U, 0U}};
}

/** Publish one boot and current Pre-operational heartbeat. */
void bootstrap(CanSocket& peer, Lifecycle& owner) {
    CHECK(peer.send(frame(0x701U, {0x00U}), 100ms).ok());
    process(owner);
    CHECK(peer.send(frame(0x701U, {0x7FU}), 100ms).ok());
    process(owner);
    CHECK(peer.send(frame(0x181U, {0x00U, 0x14U, 0x00U, 0x14U, 0U, 0U, 0U, 0U}), 100ms).ok());
    process(owner);
}

/** Build one expedited SDO upload request. */
ClassicCanFrame upload_request(const std::uint16_t index, const std::uint8_t subindex) {
    return frame(0x601U, {0x40U, static_cast<std::uint8_t>(index), static_cast<std::uint8_t>(index >> 8U), subindex, 0U,
                          0U, 0U, 0U});
}

/** Build one successful SDO download response. */
ClassicCanFrame download_response(const std::uint16_t index, const std::uint8_t subindex) {
    return frame(0x581U, {0x60U, static_cast<std::uint8_t>(index), static_cast<std::uint8_t>(index >> 8U), subindex, 0U,
                          0U, 0U, 0U});
}

/** Build one expedited SDO upload response. */
ClassicCanFrame upload_response(const std::uint16_t index, const std::uint8_t subindex,
                                const std::initializer_list<std::uint8_t> data) {
    const std::uint8_t command = data.size() == 1U ? 0x4FU : data.size() == 2U ? 0x4BU : 0x43U;
    ClassicCanFrame result = frame(0x581U, {command, static_cast<std::uint8_t>(index),
                                            static_cast<std::uint8_t>(index >> 8U), subindex, 0U, 0U, 0U, 0U});
    std::transform(data.begin(), data.end(), result.data.begin() + 4, [](const std::uint8_t value) {
        return std::byte{value};
    });
    return result;
}

/** Build one expedited SDO download request. */
ClassicCanFrame download_request(const std::uint8_t command, const std::uint16_t index, const std::uint8_t subindex,
                                 const std::initializer_list<std::uint8_t> data) {
    ClassicCanFrame result = frame(0x601U, {command, static_cast<std::uint8_t>(index),
                                            static_cast<std::uint8_t>(index >> 8U), subindex, 0U, 0U, 0U, 0U});
    std::transform(data.begin(), data.end(), result.data.begin() + 4, [](const std::uint8_t value) {
        return std::byte{value};
    });
    return result;
}

/** Answer one exact SDO download and optional exact readback. */
void answer_verified(CanSocket& peer, const ClassicCanFrame& request, const std::uint16_t index,
                     const std::uint8_t subindex, const std::initializer_list<std::uint8_t> readback,
                     const bool refresh_tpdo = false) {
    CHECK(same(receive(peer), request));
    CHECK(peer.send(download_response(index, subindex), 100ms).ok());
    if (!readback.size()) {
        return;
    }
    CHECK(same(receive(peer), upload_request(index == 0x6060U ? 0x6061U : index, subindex)));
    if (refresh_tpdo) {
        CHECK(peer.send(frame(0x181U, {0x00U, 0x14U, 0x00U, 0x14U, 0U, 0U, 0U, 0U}), 100ms).ok());
    }
    CHECK(peer.send(upload_response(index == 0x6060U ? 0x6061U : index, subindex, readback), 100ms).ok());
}

/** Return little-endian I32 bytes. */
std::array<std::uint8_t, 4> i32_bytes(const std::int32_t value) {
    const auto raw = static_cast<std::uint32_t>(value);
    return {static_cast<std::uint8_t>(raw), static_cast<std::uint8_t>(raw >> 8U), static_cast<std::uint8_t>(raw >> 16U),
            static_cast<std::uint8_t>(raw >> 24U)};
}

/** Keep one target command and readback together. */
struct TargetExchange {
    std::uint8_t subindex;
    std::int32_t value;
    std::int32_t readback;
    bool refresh_tpdo{false};
};

/** Answer one target download and exact readback. */
void answer_target(CanSocket& peer, const TargetExchange exchange) {
    const auto bytes = i32_bytes(exchange.value);
    const auto readback = i32_bytes(exchange.readback);
    answer_verified(peer, download_request(0x23U, 0x60FFU, exchange.subindex, {bytes[0], bytes[1], bytes[2], bytes[3]}),
                    0x60FFU, exchange.subindex, {readback[0], readback[1], readback[2], readback[3]},
                    exchange.refresh_tpdo);
}

/** Answer one exact expedited upload. */
void answer_upload(CanSocket& peer, const std::uint16_t index, const std::uint8_t subindex,
                   const std::initializer_list<std::uint8_t> data) {
    CHECK(same(receive(peer), upload_request(index, subindex)));
    CHECK(peer.send(upload_response(index, subindex, data), 100ms).ok());
}

/** Answer the three exact zero velocity readbacks. */
void answer_zero_feedback(CanSocket& peer) {
    answer_upload(peer, 0x606CU, 1U, {0U, 0U, 0U, 0U});
    answer_upload(peer, 0x606CU, 2U, {0U, 0U, 0U, 0U});
    answer_upload(peer, 0x606CU, 3U, {0U, 0U, 0U, 0U});
}

/** Build one TPDO1 with equal dual statuswords and packed velocity halves. */
ClassicCanFrame tpdo1(const std::uint16_t statusword, const std::int16_t low_velocity = 0,
                      const std::int16_t high_velocity = 0) {
    const auto low = static_cast<std::uint16_t>(low_velocity);
    const auto high = static_cast<std::uint16_t>(high_velocity);
    return frame(0x181U, {static_cast<std::uint8_t>(statusword), static_cast<std::uint8_t>(statusword >> 8U),
                          static_cast<std::uint8_t>(statusword), static_cast<std::uint8_t>(statusword >> 8U),
                          static_cast<std::uint8_t>(low), static_cast<std::uint8_t>(low >> 8U),
                          static_cast<std::uint8_t>(high), static_cast<std::uint8_t>(high >> 8U)});
}

/** Answer the fixed best-effort cleanup used after a failed zero-target sequence. */
void answer_cleanup(CanSocket& peer) {
    answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
    answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
    answer_verified(peer, download_request(0x2BU, 0x6040U, 0U, {0x06U, 0U}), 0x6040U, 0U, {});
    CHECK(same(receive(peer), frame(0U, {0x80U, 1U})));
}

/** Exercise the complete zero sequence and failures before further enabling. */
void test_zero_sequence(const std::string_view interface_name, CanSocket& peer, const unsigned scenario) {
    auto termination = TerminationEvent::create();
    CHECK(termination.ok());
    if (!termination.ok()) {
        return;
    }
    auto result = Lifecycle::create(config(interface_name, 2000ms, 500ms), termination.value());
    CHECK(result.ok());
    if (!result.ok()) {
        return;
    }
    auto owner = std::move(result).value();
    bootstrap(peer, *owner);
    QualificationSession session{*owner};
    std::jthread responder{[&] {
        answer_upload(peer, 0x6060U, 0U, {3U});
        answer_upload(peer, 0x6061U, 0U, {3U});
        answer_upload(peer, 0x60FFU, 1U, {0U, 0U, 0U, 0U});
        answer_upload(peer, 0x60FFU, 2U, {0U, 0U, 0U, 0U});
        answer_upload(peer, 0x603FU, 0U, {0U, 0U, 0U, 0U});
        answer_zero_feedback(peer);
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
        CHECK(same(receive(peer), frame(0U, {1U, 1U})));
        CHECK(peer.send(frame(0x701U, {5U}), 100ms).ok());
        answer_verified(peer, download_request(0x2FU, 0x6060U, 0U, {3U}), 0x6060U, 0U, {3U});
        const std::array<std::uint8_t, 3> commands{6U, 7U, 15U};
        const std::array<std::uint16_t, 3> states{0x1421U, 0x1423U, 0x1427U};
        for (std::size_t i = 0; i < commands.size(); ++i) {
            answer_verified(peer, download_request(0x2BU, 0x6040U, 0U, {commands[i], 0U}), 0x6040U, 0U, {});
            if (i == 0U && scenario != 0U && scenario != 8U) {
                auto status = tpdo1(states[i]);
                if (scenario == 1U) {
                    status.data[2] = std::byte{0x40U};
                }
                if (scenario == 2U) {
                    status.data[4] = std::byte{1U};
                }
                if (scenario == 6U) {
                    status = frame(0x081U, {1U, 0U, 1U, 0U, 0U, 0U, 0U, 0U});
                }
                if (scenario == 3U) {
                    status = frame(0x701U, {0U});
                }
                if (scenario != 4U && scenario != 5U && scenario != 7U) {
                    CHECK(peer.send(status, 100ms).ok());
                }
                if (scenario == 5U) {
                    CHECK(::kill(::getpid(), SIGTERM) == 0);
                }
                if (scenario == 7U) {
                    CHECK(same(receive(peer), download_request(0x23U, 0x60FFU, 1U, {0U, 0U, 0U, 0U})));
                } else {
                    answer_cleanup(peer);
                }
                return;
            }
            CHECK(peer.send(tpdo1(states[i]), 100ms).ok());
            answer_zero_feedback(peer);
        }
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
        answer_verified(peer, download_request(0x2BU, 0x6040U, 0U, {6U, 0U}), 0x6040U, 0U, {});
        if (scenario != 8U) {
            CHECK(peer.send(tpdo1(0x1421U), 100ms).ok());
            answer_zero_feedback(peer);
        }
        CHECK(same(receive(peer), frame(0U, {0x80U, 1U})));
        CHECK(peer.send(frame(0x701U, {0x7FU}), 100ms).ok());
    }};
    const auto qualified = session.qualify_zero_target_cia402(60ms);
    CHECK(qualified.ok() == (scenario == 0U));
    if (!qualified.ok() && scenario == 0U) {
        std::cerr << qualified.operation << " " << qualified.context << '\n';
    }
    responder.join();
    CHECK(session.state() == QualificationState::cleanup_required);
    CHECK(!session.qualify_zero_target_cia402(60ms).ok());
    expect_no_frame(peer);
}

/** Reject a wrong live mode before any write, and reject invalid sequence and duration bounds. */
void test_zero_preflight(const std::string_view interface_name, CanSocket& peer) {
    auto termination = TerminationEvent::create();
    CHECK(termination.ok());
    if (!termination.ok()) {
        return;
    }
    auto result = Lifecycle::create(config(interface_name), termination.value());
    CHECK(result.ok());
    if (!result.ok()) {
        return;
    }
    auto owner = std::move(result).value();
    bootstrap(peer, *owner);
    QualificationSession invalid{*owner};
    CHECK(!invalid.qualify_zero_target_cia402(0ms).ok());
    expect_no_frame(peer);
    QualificationSession duration_bound{*owner};
    CHECK(!duration_bound.qualify_first_motion_cia402(IndependentChannel::subindex_1, 5, 3001ms, 60ms).ok());
    expect_no_frame(peer);
    QualificationSession session{*owner};
    std::jthread responder{[&] {
        answer_upload(peer, 0x6060U, 0U, {1U});
    }};
    CHECK(!session.qualify_zero_target_cia402(60ms).ok());
    responder.join();
    CHECK(session.state() == QualificationState::cleanup_required);
    expect_no_frame(peer);
}

/** Exercise one complete per-channel first-motion sequence with verified volatile restoration. */
void test_first_motion(const std::string_view interface_name, CanSocket& peer, const IndependentChannel channel) {
    auto termination = TerminationEvent::create();
    CHECK(termination.ok());
    auto owner_result = Lifecycle::create(config(interface_name, 2000ms, 500ms), termination.value());
    CHECK(owner_result.ok());
    if (!termination.ok() || !owner_result.ok()) {
        return;
    }
    auto owner = std::move(owner_result).value();
    bootstrap(peer, *owner);
    QualificationSession session{*owner};
    std::jthread responder{[&] {
        answer_upload(peer, 0x6060U, 0U, {3U});
        answer_upload(peer, 0x6061U, 0U, {3U});
        answer_upload(peer, 0x60FFU, 1U, {0U, 0U, 0U, 0U});
        answer_upload(peer, 0x60FFU, 2U, {0U, 0U, 0U, 0U});
        answer_upload(peer, 0x603FU, 0U, {0U, 0U, 0U, 0U});
        answer_zero_feedback(peer);
        answer_upload(peer, 0x200FU, 0U, {1U, 0U});
        answer_verified(peer, download_request(0x2BU, 0x200FU, 0U, {0U, 0U}), 0x200FU, 0U, {0U, 0U});
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
        CHECK(same(receive(peer), frame(0U, {1U, 1U})));
        CHECK(peer.send(frame(0x701U, {5U}), 100ms).ok());
        answer_verified(peer, download_request(0x2FU, 0x6060U, 0U, {3U}), 0x6060U, 0U, {3U});
        for (const auto& [command, state] : std::array{std::pair{std::uint8_t{6U}, std::uint16_t{0x1421U}},
                                                       std::pair{std::uint8_t{7U}, std::uint16_t{0x1423U}},
                                                       std::pair{std::uint8_t{15U}, std::uint16_t{0x1427U}}}) {
            answer_verified(peer, download_request(0x2BU, 0x6040U, 0U, {command, 0U}), 0x6040U, 0U, {});
            CHECK(peer.send(tpdo1(state), 100ms).ok());
            answer_zero_feedback(peer);
        }
        const auto commanded_subindex = static_cast<std::uint8_t>(channel);
        answer_target(peer, {.subindex = commanded_subindex, .value = 5, .readback = 5});
        CHECK(
            peer.send(channel == IndependentChannel::subindex_1 ? tpdo1(0x1427U, 50, 0) : tpdo1(0x1427U, 0, 50), 100ms)
                .ok());
        answer_target(peer, {.subindex = commanded_subindex, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
        answer_verified(peer, download_request(0x2BU, 0x6040U, 0U, {6U, 0U}), 0x6040U, 0U, {});
        CHECK(peer.send(tpdo1(0x1421U), 100ms).ok());
        if (channel == IndependentChannel::subindex_1) {
            answer_upload(peer, 0x606CU, 1U, {16U, 0U, 0U, 0U});
        } else {
            answer_upload(peer, 0x606CU, 1U, {0U, 0U, 0U, 0U});
            answer_upload(peer, 0x606CU, 2U, {16U, 0U, 0U, 0U});
        }
        answer_zero_feedback(peer);
        CHECK(same(receive(peer), frame(0U, {0x80U, 1U})));
        CHECK(peer.send(frame(0x701U, {0x7FU}), 100ms).ok());
        answer_verified(peer, download_request(0x2BU, 0x200FU, 0U, {1U, 0U}), 0x200FU, 0U, {1U, 0U});
    }};
    CHECK(session.qualify_first_motion_cia402(channel, 5, 20ms, 60ms).ok());
    responder.join();
    CHECK(session.state() == QualificationState::cleanup_required);
    CHECK(!session.qualify_first_motion_cia402(channel, 5, 20ms, 60ms).ok());
    expect_no_frame(peer);
}

/** Verify one bounded channel-2 motion is stopped by NMT Stopped before target cleanup. */
void test_nmt_stop_motion(const std::string_view interface_name, CanSocket& peer) {
    auto termination = TerminationEvent::create();
    CHECK(termination.ok());
    auto owner_result = Lifecycle::create(config(interface_name, 2000ms, 500ms), termination.value());
    CHECK(owner_result.ok());
    if (!termination.ok() || !owner_result.ok()) {
        return;
    }
    auto owner = std::move(owner_result).value();
    QualificationSession session{*owner};
    std::jthread responder{[&] {
        answer_upload(peer, 0x1017U, 0U, {0U, 0U});
        answer_verified(peer, download_request(0x2BU, 0x1017U, 0U, {0xF4U, 0x01U}), 0x1017U, 0U, {0xF4U, 0x01U});
        CHECK(peer.send(frame(0x701U, {0x7FU}), 100ms).ok());
        answer_upload(peer, 0x6060U, 0U, {3U});
        answer_upload(peer, 0x6061U, 0U, {3U});
        answer_upload(peer, 0x60FFU, 1U, {0U, 0U, 0U, 0U});
        answer_upload(peer, 0x60FFU, 2U, {0U, 0U, 0U, 0U});
        answer_upload(peer, 0x603FU, 0U, {0U, 0U, 0U, 0U});
        answer_zero_feedback(peer);
        answer_upload(peer, 0x200FU, 0U, {1U, 0U});
        answer_verified(peer, download_request(0x2BU, 0x200FU, 0U, {0U, 0U}), 0x200FU, 0U, {0U, 0U});
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
        CHECK(same(receive(peer), frame(0U, {1U, 1U})));
        CHECK(peer.send(frame(0x701U, {5U}), 100ms).ok());
        answer_verified(peer, download_request(0x2FU, 0x6060U, 0U, {3U}), 0x6060U, 0U, {3U});
        for (const auto& [command, state] : std::array{std::pair{std::uint8_t{6U}, std::uint16_t{0x1421U}},
                                                       std::pair{std::uint8_t{7U}, std::uint16_t{0x1423U}},
                                                       std::pair{std::uint8_t{15U}, std::uint16_t{0x1427U}}}) {
            answer_verified(peer, download_request(0x2BU, 0x6040U, 0U, {command, 0U}), 0x6040U, 0U, {});
            CHECK(peer.send(tpdo1(state), 100ms).ok());
            answer_zero_feedback(peer);
        }
        answer_target(peer, {.subindex = 2U, .value = 5, .readback = 5});
        CHECK(peer.send(tpdo1(0x1427U), 100ms).ok());
        CHECK(same(receive(peer), frame(0U, {2U, 1U})));
        CHECK(peer.send(frame(0x701U, {4U}), 100ms).ok());
        CHECK(same(receive(peer), frame(0U, {0x80U, 1U})));
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
        CHECK(peer.send(frame(0x701U, {0x7FU}), 100ms).ok());
        CHECK(same(receive(peer), frame(0U, {1U, 1U})));
        CHECK(peer.send(frame(0x701U, {5U}), 100ms).ok());
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
        answer_verified(peer, download_request(0x2BU, 0x6040U, 0U, {6U, 0U}), 0x6040U, 0U, {});
        CHECK(peer.send(tpdo1(0x1421U), 100ms).ok());
        answer_zero_feedback(peer);
        CHECK(same(receive(peer), frame(0U, {0x80U, 1U})));
        CHECK(peer.send(frame(0x701U, {0x7FU}), 100ms).ok());
        answer_verified(peer, download_request(0x2BU, 0x200FU, 0U, {1U, 0U}), 0x200FU, 0U, {1U, 0U});
        answer_verified(peer, download_request(0x2BU, 0x1017U, 0U, {0U, 0U}), 0x1017U, 0U, {0U, 0U});
    }};
    CHECK(session.qualify_nmt_stop_cia402(IndependentChannel::subindex_2, 5, 20ms, 60ms).ok());
    responder.join();
    CHECK(session.state() == QualificationState::cleanup_required);
    CHECK(!session.qualify_nmt_stop_cia402(IndependentChannel::subindex_2, 5, 20ms, 60ms).ok());
    expect_no_frame(peer);
}

/** Verify one bounded channel-2 motion is stopped by exactly one terminal controlword before target cleanup. */
void test_controlword_stop_motion(const std::string_view interface_name, CanSocket& peer,
                                  const TransitionControlword controlword, const bool acknowledge_stop,
                                  const std::uint16_t quick_stop_option = 5U) {
    auto termination = TerminationEvent::create();
    CHECK(termination.ok());
    auto owner_result = Lifecycle::create(config(interface_name, 2000ms, 500ms), termination.value());
    CHECK(owner_result.ok());
    if (!termination.ok() || !owner_result.ok()) {
        return;
    }
    auto owner = std::move(owner_result).value();
    QualificationSession session{*owner};
    std::jthread responder{[&] {
        if (controlword == TransitionControlword::quick_stop) {
            answer_upload(
                peer, 0x605AU, 0U,
                {static_cast<std::uint8_t>(quick_stop_option), static_cast<std::uint8_t>(quick_stop_option >> 8U)});
            if (quick_stop_option != 5U) {
                return;
            }
        }
        answer_upload(peer, 0x1017U, 0U, {0U, 0U});
        answer_verified(peer, download_request(0x2BU, 0x1017U, 0U, {0xF4U, 0x01U}), 0x1017U, 0U, {0xF4U, 0x01U});
        CHECK(peer.send(frame(0x701U, {0x7FU}), 100ms).ok());
        answer_upload(peer, 0x6060U, 0U, {3U});
        answer_upload(peer, 0x6061U, 0U, {3U});
        answer_upload(peer, 0x60FFU, 1U, {0U, 0U, 0U, 0U});
        answer_upload(peer, 0x60FFU, 2U, {0U, 0U, 0U, 0U});
        answer_upload(peer, 0x603FU, 0U, {0U, 0U, 0U, 0U});
        answer_zero_feedback(peer);
        answer_upload(peer, 0x200FU, 0U, {1U, 0U});
        answer_verified(peer, download_request(0x2BU, 0x200FU, 0U, {0U, 0U}), 0x200FU, 0U, {0U, 0U});
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
        CHECK(same(receive(peer), frame(0U, {1U, 1U})));
        CHECK(peer.send(frame(0x701U, {5U}), 100ms).ok());
        answer_verified(peer, download_request(0x2FU, 0x6060U, 0U, {3U}), 0x6060U, 0U, {3U});
        for (const auto& [command, state] : std::array{std::pair{std::uint8_t{6U}, std::uint16_t{0x1421U}},
                                                       std::pair{std::uint8_t{7U}, std::uint16_t{0x1423U}},
                                                       std::pair{std::uint8_t{15U}, std::uint16_t{0x1427U}}}) {
            answer_verified(peer, download_request(0x2BU, 0x6040U, 0U, {command, 0U}), 0x6040U, 0U, {});
            CHECK(peer.send(tpdo1(state), 100ms).ok());
            answer_zero_feedback(peer);
        }
        answer_target(peer, {.subindex = 2U, .value = 5, .readback = 5});
        CHECK(peer.send(tpdo1(0x1427U), 100ms).ok());
        const auto [controlword_byte, stopped_status] = [&] {
            switch (controlword) {
                case TransitionControlword::disable_voltage:
                    return std::pair{std::uint8_t{0U}, std::uint16_t{0x1460U}};
                case TransitionControlword::quick_stop:
                    return std::pair{std::uint8_t{2U}, std::uint16_t{0x1407U}};
                case TransitionControlword::shutdown:
                    return std::pair{std::uint8_t{6U}, std::uint16_t{0x1421U}};
                case TransitionControlword::switch_on:
                case TransitionControlword::enable_operation:
                    break;
            }
            return std::pair{std::uint8_t{0xFFU}, std::uint16_t{0xFFFFU}};
        }();
        if (acknowledge_stop) {
            answer_verified(peer, download_request(0x2BU, 0x6040U, 0U, {controlword_byte, 0U}), 0x6040U, 0U, {});
            CHECK(peer.send(tpdo1(stopped_status), 100ms).ok());
            answer_zero_feedback(peer);
        } else {
            CHECK(same(receive(peer), download_request(0x2BU, 0x6040U, 0U, {controlword_byte, 0U})));
        }
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
        answer_zero_feedback(peer);
        CHECK(same(receive(peer), frame(0U, {0x80U, 1U})));
        CHECK(peer.send(frame(0x701U, {0x7FU}), 100ms).ok());
        answer_verified(peer, download_request(0x2BU, 0x200FU, 0U, {1U, 0U}), 0x200FU, 0U, {1U, 0U});
        answer_verified(peer, download_request(0x2BU, 0x1017U, 0U, {0U, 0U}), 0x1017U, 0U, {0U, 0U});
    }};
    const auto result = [&] {
        switch (controlword) {
            case TransitionControlword::disable_voltage:
                return session.qualify_disable_voltage_cia402(IndependentChannel::subindex_2, 5, 20ms, 60ms);
            case TransitionControlword::quick_stop:
                return session.qualify_quick_stop_cia402(IndependentChannel::subindex_2, 5, 20ms, 60ms);
            case TransitionControlword::shutdown:
                return session.qualify_shutdown_cia402(IndependentChannel::subindex_2, 5, 20ms, 60ms);
            case TransitionControlword::switch_on:
            case TransitionControlword::enable_operation:
                break;
        }
        return robot_control::platform::linux::Status::from_errno("invalid_test_controlword", "vcan", EINVAL);
    }();
    CHECK(result.ok() == (acknowledge_stop && quick_stop_option == 5U));
    responder.join();
    CHECK(session.state() == QualificationState::cleanup_required);
    expect_no_frame(peer);
}

/** Keep the drive inhibited when the expected NMT Stopped heartbeat times out. */
void test_nmt_stop_timeout_stays_preoperational(const std::string_view interface_name, CanSocket& peer) {
    auto termination = TerminationEvent::create();
    CHECK(termination.ok());
    auto owner_result = Lifecycle::create(config(interface_name, 2000ms, 500ms), termination.value());
    CHECK(owner_result.ok());
    if (!termination.ok() || !owner_result.ok()) {
        return;
    }
    auto owner = std::move(owner_result).value();
    QualificationSession session{*owner};
    std::jthread responder{[&] {
        answer_upload(peer, 0x1017U, 0U, {0U, 0U});
        answer_verified(peer, download_request(0x2BU, 0x1017U, 0U, {0xF4U, 0x01U}), 0x1017U, 0U, {0xF4U, 0x01U});
        CHECK(peer.send(frame(0x701U, {0x7FU}), 100ms).ok());
        answer_upload(peer, 0x6060U, 0U, {3U});
        answer_upload(peer, 0x6061U, 0U, {3U});
        answer_upload(peer, 0x60FFU, 1U, {0U, 0U, 0U, 0U});
        answer_upload(peer, 0x60FFU, 2U, {0U, 0U, 0U, 0U});
        answer_upload(peer, 0x603FU, 0U, {0U, 0U, 0U, 0U});
        answer_zero_feedback(peer);
        answer_upload(peer, 0x200FU, 0U, {1U, 0U});
        answer_verified(peer, download_request(0x2BU, 0x200FU, 0U, {0U, 0U}), 0x200FU, 0U, {0U, 0U});
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
        CHECK(same(receive(peer), frame(0U, {1U, 1U})));
        CHECK(peer.send(frame(0x701U, {5U}), 100ms).ok());
        answer_verified(peer, download_request(0x2FU, 0x6060U, 0U, {3U}), 0x6060U, 0U, {3U});
        for (const auto& [command, state] : std::array{std::pair{std::uint8_t{6U}, std::uint16_t{0x1421U}},
                                                       std::pair{std::uint8_t{7U}, std::uint16_t{0x1423U}},
                                                       std::pair{std::uint8_t{15U}, std::uint16_t{0x1427U}}}) {
            answer_verified(peer, download_request(0x2BU, 0x6040U, 0U, {command, 0U}), 0x6040U, 0U, {});
            CHECK(peer.send(tpdo1(state), 100ms).ok());
            answer_zero_feedback(peer);
        }
        answer_target(peer, {.subindex = 2U, .value = 5, .readback = 5});
        CHECK(peer.send(tpdo1(0x1427U), 100ms).ok());
        CHECK(same(receive(peer), frame(0U, {2U, 1U})));
        CHECK(same(receive(peer, 100ms), frame(0U, {0x80U, 1U})));
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
        CHECK(peer.send(frame(0x701U, {0x7FU}), 100ms).ok());
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
        answer_verified(peer, download_request(0x2BU, 0x6040U, 0U, {6U, 0U}), 0x6040U, 0U, {});
        CHECK(peer.send(tpdo1(0x1421U), 100ms).ok());
        answer_zero_feedback(peer);
        CHECK(same(receive(peer), frame(0U, {0x80U, 1U})));
        CHECK(peer.send(frame(0x701U, {0x7FU}), 100ms).ok());
        answer_verified(peer, download_request(0x2BU, 0x200FU, 0U, {1U, 0U}), 0x200FU, 0U, {1U, 0U});
        answer_verified(peer, download_request(0x2BU, 0x1017U, 0U, {0U, 0U}), 0x1017U, 0U, {0U, 0U});
    }};
    const auto result = session.qualify_nmt_stop_cia402(IndependentChannel::subindex_2, 5, 20ms, 20ms);
    CHECK(!result.ok());
    CHECK(result.operation == "qualification_nmt_timeout");
    responder.join();
    CHECK(session.state() == QualificationState::cleanup_required);
    expect_no_frame(peer);
}

/** Verify 0x200F restoration after an enabled-state transition timeout. */
void test_first_motion_failure_restores_application(const std::string_view interface_name, CanSocket& peer) {
    auto termination = TerminationEvent::create();
    auto owner_result = Lifecycle::create(config(interface_name, 2000ms, 500ms), termination.value());
    CHECK(termination.ok());
    CHECK(owner_result.ok());
    if (!termination.ok() || !owner_result.ok()) {
        return;
    }
    auto owner = std::move(owner_result).value();
    bootstrap(peer, *owner);
    QualificationSession session{*owner};
    std::jthread responder{[&] {
        answer_upload(peer, 0x6060U, 0U, {3U});
        answer_upload(peer, 0x6061U, 0U, {3U});
        answer_upload(peer, 0x60FFU, 1U, {0U, 0U, 0U, 0U});
        answer_upload(peer, 0x60FFU, 2U, {0U, 0U, 0U, 0U});
        answer_upload(peer, 0x603FU, 0U, {0U, 0U, 0U, 0U});
        answer_zero_feedback(peer);
        answer_upload(peer, 0x200FU, 0U, {1U, 0U});
        answer_verified(peer, download_request(0x2BU, 0x200FU, 0U, {0U, 0U}), 0x200FU, 0U, {0U, 0U});
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
        CHECK(same(receive(peer), frame(0U, {1U, 1U})));
        CHECK(peer.send(frame(0x701U, {5U}), 100ms).ok());
        answer_verified(peer, download_request(0x2FU, 0x6060U, 0U, {3U}), 0x6060U, 0U, {3U});
        answer_verified(peer, download_request(0x2BU, 0x6040U, 0U, {6U, 0U}), 0x6040U, 0U, {});
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
        answer_verified(peer, download_request(0x2BU, 0x6040U, 0U, {6U, 0U}), 0x6040U, 0U, {});
        CHECK(peer.send(tpdo1(0x1421U), 100ms).ok());
        answer_zero_feedback(peer);
        CHECK(same(receive(peer), frame(0U, {0x80U, 1U})));
        CHECK(peer.send(frame(0x701U, {0x7FU}), 100ms).ok());
        answer_verified(peer, download_request(0x2BU, 0x200FU, 0U, {1U, 0U}), 0x200FU, 0U, {1U, 0U});
    }};
    const auto result = session.qualify_first_motion_cia402(IndependentChannel::subindex_1, 5, 20ms, 30ms);
    CHECK(!result.ok());
    CHECK(result.operation == "qualification_dual_state_timeout");
    responder.join();
    CHECK(session.state() == QualificationState::cleanup_required);
    expect_no_frame(peer);
}

/** Reject a target when both status halves are not Operation Enabled. */
void test_target_requires_enabled(const std::string_view interface_name, CanSocket& peer) {
    auto termination = TerminationEvent::create();
    auto owner_result = Lifecycle::create(config(interface_name), termination.value());
    CHECK(termination.ok());
    CHECK(owner_result.ok());
    if (!termination.ok() || !owner_result.ok()) {
        return;
    }
    auto owner = std::move(owner_result).value();
    bootstrap(peer, *owner);
    QualificationSession session{*owner};
    std::jthread responder{[&] {
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
    }};
    CHECK(!session.run_target_once(IndependentChannel::subindex_1, 5, 20ms).ok());
    responder.join();
    CHECK(session.state() == QualificationState::cleanup_required);
    expect_no_frame(peer);
}

/** Reject a target while the uncommanded protocol velocity half is nonzero. */
void test_other_channel_motion_rejected(const std::string_view interface_name, CanSocket& peer) {
    auto termination = TerminationEvent::create();
    auto owner_result = Lifecycle::create(config(interface_name), termination.value());
    CHECK(termination.ok());
    CHECK(owner_result.ok());
    if (!termination.ok() || !owner_result.ok()) {
        return;
    }
    auto owner = std::move(owner_result).value();
    bootstrap(peer, *owner);
    CHECK(peer.send(tpdo1(0x1427U, 0, 1), 100ms).ok());
    process(*owner);
    QualificationSession session{*owner};
    std::jthread responder{[&] {
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
    }};
    CHECK(!session.run_target_once(IndependentChannel::subindex_1, 5, 20ms).ok());
    responder.join();
    CHECK(session.state() == QualificationState::cleanup_required);
    expect_no_frame(peer);
}

/** Set one namespace-local vcan interface administrative state. */
bool set_interface_up(const std::string_view interface_name, const bool requested_up) {
    const int descriptor = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (descriptor < 0 || interface_name.empty() || interface_name.size() >= IFNAMSIZ) {
        if (descriptor >= 0) {
            static_cast<void>(::close(descriptor));
        }
        return false;
    }
    UniqueFd control{descriptor};
    ifreq request{};
    interface_name.copy(request.ifr_name, interface_name.size());
    request.ifr_name[interface_name.size()] = '\0';
    if (::ioctl(control.get(), SIOCGIFFLAGS, &request) != 0) {
        return false;
    }
    request.ifr_flags =
        requested_up ? static_cast<short>(request.ifr_flags | IFF_UP) : static_cast<short>(request.ifr_flags & ~IFF_UP);
    return ::ioctl(control.get(), SIOCSIFFLAGS, &request) == 0;
}

/** Exercise allowed operations and the non-renewable target sequence. */
void test_success(const char* interface_name, CanSocket& peer) {
    auto termination = TerminationEvent::create();
    CHECK(termination.ok());
    auto owner_result = Lifecycle::create(config(interface_name), termination.value());
    CHECK(owner_result.ok());
    if (!termination.ok() || !owner_result.ok()) {
        return;
    }
    auto owner = std::move(owner_result).value();
    bootstrap(peer, *owner);
    QualificationSession session{*owner};

    for (const auto& [command, byte] :
         std::array{std::pair{QualificationNmt::operational, 0x01U}, std::pair{QualificationNmt::stopped, 0x02U},
                    std::pair{QualificationNmt::pre_operational, 0x80U}}) {
        CHECK(session.send_nmt(command).ok());
        CHECK(same(receive(peer), frame(0U, {static_cast<std::uint8_t>(byte), 1U})));
    }

    std::jthread mode_peer{[&] {
        answer_verified(peer, download_request(0x2FU, 0x6060U, 0U, {3U}), 0x6060U, 0U, {3U});
    }};
    CHECK(session.set_velocity_mode().ok());
    mode_peer.join();

    for (const auto& [controlword, low] : std::array{std::pair{TransitionControlword::shutdown, 0x06U},
                                                     std::pair{TransitionControlword::switch_on, 0x07U},
                                                     std::pair{TransitionControlword::enable_operation, 0x0FU}}) {
        std::jthread control_peer{[&] {
            answer_verified(peer, download_request(0x2BU, 0x6040U, 0U, {static_cast<std::uint8_t>(low), 0U}), 0x6040U,
                            0U, {});
        }};
        CHECK(session.send_controlword(controlword).ok());
        control_peer.join();
    }

    std::jthread zero_peer{[&] {
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
    }};
    CHECK(session.set_zero_targets().ok());
    zero_peer.join();
    CHECK(peer.send(tpdo1(0x1427U), 100ms).ok());
    process(*owner);

    std::jthread target_peer{[&] {
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 1U, .value = 10, .readback = 10});
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
    }};
    CHECK(session.run_target_once(IndependentChannel::subindex_1, 10, 20ms).ok());
    target_peer.join();
    CHECK(session.state() == QualificationState::ready);
    CHECK(!session.run_target_once(IndependentChannel::subindex_1, 10, 20ms).ok());
    CHECK(session.state() == QualificationState::cleanup_required);
    expect_no_frame(peer);

    std::jthread cleanup_peer{[&] {
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
        answer_verified(peer, download_request(0x2BU, 0x6040U, 0U, {0x06U, 0U}), 0x6040U, 0U, {});
        CHECK(same(receive(peer), frame(0U, {0x80U, 1U})));
    }};
    CHECK(session.cleanup().ok());
    cleanup_peer.join();
    CHECK(session.state() == QualificationState::cleanup_required);
}

/** Verify a delayed nonzero readback cannot extend the deadline before the zero request. */
void test_target_deadline_forces_zero(const char* interface_name, CanSocket& peer) {
    auto termination = TerminationEvent::create();
    auto owner_result = Lifecycle::create(config(interface_name, 2000ms, 500ms), termination.value());
    CHECK(termination.ok());
    CHECK(owner_result.ok());
    if (!termination.ok() || !owner_result.ok()) {
        return;
    }
    auto owner = std::move(owner_result).value();
    bootstrap(peer, *owner);
    CHECK(peer.send(tpdo1(0x1427U), 100ms).ok());
    process(*owner);
    QualificationSession session{*owner};

    std::jthread responder{[&] {
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
        const auto nonzero_at = std::chrono::steady_clock::now();
        CHECK(same(receive(peer), download_request(0x23U, 0x60FFU, 1U, {5U, 0U, 0U, 0U})));
        CHECK(peer.send(download_response(0x60FFU, 1U), 100ms).ok());
        CHECK(same(receive(peer), upload_request(0x60FFU, 1U)));
        CHECK(same(receive(peer, 100ms), download_request(0x23U, 0x60FFU, 1U, {0U, 0U, 0U, 0U})));
        CHECK(std::chrono::steady_clock::now() - nonzero_at <= 60ms);
        CHECK(peer.send(download_response(0x60FFU, 1U), 100ms).ok());
        CHECK(same(receive(peer), upload_request(0x60FFU, 1U)));
        CHECK(peer.send(upload_response(0x60FFU, 1U, {0U, 0U, 0U, 0U}), 100ms).ok());
    }};
    const auto result = session.run_target_once(IndependentChannel::subindex_1, 5, 20ms);
    CHECK(!result.ok());
    CHECK(result.operation == "qualification_upload");
    responder.join();
    CHECK(session.state() == QualificationState::cleanup_required);
}

/** Exercise timeout, no retry, late-response rejection, and permanent inhibit. */
void test_timeout(const char* interface_name, CanSocket& peer) {
    auto termination = TerminationEvent::create();
    auto owner_result = Lifecycle::create(config(interface_name), termination.value());
    CHECK(termination.ok());
    CHECK(owner_result.ok());
    if (!termination.ok() || !owner_result.ok()) {
        return;
    }
    auto owner = std::move(owner_result).value();
    bootstrap(peer, *owner);
    QualificationSession session{*owner};
    const auto before = owner->observation_snapshot(std::chrono::steady_clock::now()).sdo_rejection_count;
    std::jthread timeout_peer{[&] {
        CHECK(same(receive(peer), download_request(0x2FU, 0x6060U, 0U, {3U})));
        std::this_thread::sleep_for(40ms);
        CHECK(peer.send(download_response(0x6060U, 0U), 100ms).ok());
    }};
    CHECK(!session.set_velocity_mode().ok());
    timeout_peer.join();
    process(*owner);
    CHECK(session.state() == QualificationState::cleanup_required);
    CHECK(owner->observation_snapshot(std::chrono::steady_clock::now()).sdo_rejection_count >= before + 1U);
    expect_no_frame(peer);
    CHECK(!session.send_nmt(QualificationNmt::operational).ok());
    expect_no_frame(peer);
}

/** Exercise a partial target sequence, failed readback, and best-effort zero. */
void test_partial_sequence(const char* interface_name, CanSocket& peer) {
    auto termination = TerminationEvent::create();
    auto owner_result = Lifecycle::create(config(interface_name), termination.value());
    CHECK(termination.ok());
    CHECK(owner_result.ok());
    if (!termination.ok() || !owner_result.ok()) {
        return;
    }
    auto owner = std::move(owner_result).value();
    bootstrap(peer, *owner);
    CHECK(peer.send(tpdo1(0x1427U), 100ms).ok());
    process(*owner);
    QualificationSession session{*owner};
    std::jthread partial_peer{[&] {
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = -5, .readback = -4});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
    }};
    CHECK(!session.run_target_once(IndependentChannel::subindex_2, -5, 100ms).ok());
    partial_peer.join();
    CHECK(session.state() == QualificationState::cleanup_required);
}

/** Exercise feedback staleness during a nonzero interval and verified zero. */
void test_feedback_loss(const char* interface_name, CanSocket& peer) {
    auto termination = TerminationEvent::create();
    auto owner_result = Lifecycle::create(config(interface_name, 2000ms, 15ms), termination.value());
    CHECK(termination.ok());
    CHECK(owner_result.ok());
    if (!termination.ok() || !owner_result.ok()) {
        return;
    }
    auto owner = std::move(owner_result).value();
    bootstrap(peer, *owner);
    CHECK(peer.send(tpdo1(0x1427U), 100ms).ok());
    process(*owner);
    QualificationSession session{*owner};
    std::jthread feedback_peer{[&] {
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
        CHECK(peer.send(tpdo1(0x1427U), 100ms).ok());
        answer_target(peer, {.subindex = 1U, .value = 5, .readback = 5});
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
    }};
    CHECK(!session.run_target_once(IndependentChannel::subindex_1, 5, 100ms).ok());
    feedback_peer.join();
    CHECK(session.state() == QualificationState::cleanup_required);
}

/** Exercise stale-call rejection without any outgoing frame. */
void test_stale(const char* interface_name, CanSocket& peer) {
    auto termination = TerminationEvent::create();
    auto owner_result = Lifecycle::create(config(interface_name, 20ms), termination.value());
    CHECK(termination.ok());
    CHECK(owner_result.ok());
    if (!termination.ok() || !owner_result.ok()) {
        return;
    }
    auto owner = std::move(owner_result).value();
    bootstrap(peer, *owner);
    process(*owner, 30ms);
    QualificationSession session{*owner};
    CHECK(!session.send_nmt(QualificationNmt::operational).ok());
    CHECK(session.state() == QualificationState::cleanup_required);
    expect_no_frame(peer);
    std::jthread zero_peer{[&] {
        answer_target(peer, {.subindex = 1U, .value = 0, .readback = 0});
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
    }};
    CHECK(session.set_zero_targets().ok());
    zero_peer.join();
    CHECK(session.state() == QualificationState::cleanup_required);
}

/** Exercise SIGTERM interruption during an incomplete SDO operation. */
void test_signal(const char* interface_name, CanSocket& peer) {
    auto termination = TerminationEvent::create();
    auto owner_result = Lifecycle::create(config(interface_name), termination.value());
    CHECK(termination.ok());
    CHECK(owner_result.ok());
    if (!termination.ok() || !owner_result.ok()) {
        return;
    }
    auto owner = std::move(owner_result).value();
    bootstrap(peer, *owner);
    QualificationSession session{*owner};
    std::jthread signal_peer{[&] {
        CHECK(same(receive(peer), download_request(0x2FU, 0x6060U, 0U, {3U})));
        CHECK(::kill(::getpid(), SIGTERM) == 0);
    }};
    CHECK(!session.set_velocity_mode().ok());
    signal_peer.join();
    CHECK(session.state() == QualificationState::cleanup_required);
    expect_no_frame(peer);
}

/** Exercise interface loss during an incomplete SDO operation. */
void test_link_loss(const char* interface_name, CanSocket& peer) {
    auto termination = TerminationEvent::create();
    auto owner_result = Lifecycle::create(config(interface_name), termination.value());
    CHECK(termination.ok());
    CHECK(owner_result.ok());
    if (!termination.ok() || !owner_result.ok()) {
        return;
    }
    auto owner = std::move(owner_result).value();
    bootstrap(peer, *owner);
    QualificationSession session{*owner};
    std::jthread link_peer{[&] {
        CHECK(same(receive(peer), download_request(0x2FU, 0x6060U, 0U, {3U})));
        CHECK(set_interface_up(interface_name, false));
    }};
    CHECK(!session.set_velocity_mode().ok());
    link_peer.join();
    CHECK(session.state() == QualificationState::cleanup_required);
    CHECK(set_interface_up(interface_name, true));
}

/** Prove an axis-1 cleanup failure cannot skip the axis-2 zero attempt. */
void test_zero_both_after_failure(const std::string_view interface_name, CanSocket& peer) {
    auto termination = TerminationEvent::create();
    auto owner = Lifecycle::create(config(interface_name), termination.value());
    CHECK(owner.ok());
    if (!owner.ok()) {
        return;
    }
    QualificationSession session{*owner.value()};
    std::jthread responder{[&] {
        CHECK(same(receive(peer), download_request(0x23U, 0x60FFU, 1U, {0U, 0U, 0U, 0U})));
        answer_target(peer, {.subindex = 2U, .value = 0, .readback = 0});
    }};
    CHECK(!session.set_zero_targets().ok());
    responder.join();
    expect_no_frame(peer);
}

/** Exercise wire loss, silence attribution and failed restoration without real hardware. */
void test_communication_loss(const std::string_view interface_name, CanSocket& peer,
                             const CommunicationLossStimulus stimulus, const unsigned scenario) {
    auto termination = TerminationEvent::create();
    auto owner = Lifecycle::create(config(interface_name, 500ms, 100ms), termination.value());
    CHECK(owner.ok());
    if (!owner.ok()) {
        return;
    }
    QualificationSession session{*owner.value()};
    unsigned nonzero = 0U;
    bool zero_attempted = false;
    bool stopped_by_watchdog = false;
    bool watchdog_restored = false;
    bool suppression_seen = false;
    unsigned motion_probe_parts = 0U;
    std::jthread responder{[&](const std::stop_token stop) {
        std::map<std::pair<std::uint16_t, std::uint8_t>, std::uint32_t> values{
            {{0x2000U, 0U}, scenario == 5U ? 1U : 0U},
            {{0x1800U, 5U}, scenario == 9U ? 101U : 100U},
            {{0x1017U, 0U}, 0U},
            {{0x200FU, 0U}, 1U},
            {{0x6060U, 0U}, 3U},
            {{0x6061U, 0U}, 3U}};
        std::uint16_t statusword = 0x1421U;
        std::uint8_t nmt = 0x7FU;
        auto last_host = std::chrono::steady_clock::now();
        auto last_hb = last_host;
        auto last_tpdo = last_host;
        auto target_readback = last_host;
        auto motion_probe_completed = last_host;
        bool moving = false;
        bool injected = false;
        while (!stop.stop_requested()) {
            const auto now = std::chrono::steady_clock::now();
            if (nonzero && !injected && now - target_readback >= 250ms && (scenario == 7U || scenario == 8U)) {
                injected = true;
                if (scenario == 7U) {
                    CHECK(::kill(::getpid(), SIGTERM) == 0);
                } else {
                    CHECK(peer.send(frame(0x081U, {1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}), 100ms).ok());
                }
            }
            if (moving && values[{0x2000U, 0U}] == 1000U && now - last_host >= 1000ms && scenario != 1U) {
                moving = false;
                stopped_by_watchdog = true;
            }
            bool hb = values[{0x1017U, 0U}] != 0U;
            bool tpdo = values[{0x1800U, 5U}] != 0U;
            if (suppression_seen && scenario == 2U) {
                hb = tpdo = true;
            }
            if (suppression_seen && scenario == 3U) {
                hb = stimulus != CommunicationLossStimulus::tpdo;
                tpdo = stimulus != CommunicationLossStimulus::heartbeat;
            }
            if (hb && now - last_hb >= 20ms) {
                CHECK(peer.send(frame(0x701U, {nmt}), 100ms).ok());
                last_hb = now;
            }
            if (tpdo && nmt == 5U && now - last_tpdo >= 10ms) {
                CHECK(peer.send(tpdo1(statusword), 100ms).ok());
                last_tpdo = now;
            }
            const auto received = peer.receive(1ms);
            CHECK(received.ok());
            if (!received.ok() || !received.value()) {
                continue;
            }
            const auto request = received.value()->frame;
            if (request.raw_can_id == 0U) {
                CHECK(request.data[0] == std::byte{1U} || request.data[0] == std::byte{0x80U});
                nmt = request.data[0] == std::byte{1U} ? 5U : 0x7FU;
                last_host = now;
                continue;
            }
            CHECK(request.raw_can_id == 0x601U);
            const auto index = static_cast<std::uint16_t>(std::to_integer<unsigned>(request.data[1])
                                                          | std::to_integer<unsigned>(request.data[2]) << 8U);
            const auto sub = std::to_integer<std::uint8_t>(request.data[3]);
            const auto key = std::pair{index, sub};
            const bool upload = request.data[0] == std::byte{0x40U};
            if (nonzero && !zero_attempted && motion_probe_parts == 2U && scenario < 10U && scenario != 7U
                && scenario != 8U && stimulus == CommunicationLossStimulus::watchdog && !(upload && index == 0x60FFU)) {
                CHECK(now - motion_probe_completed >= 1500ms);
            }
            last_host = now;
            if (!upload) {
                std::uint32_t value = 0U;
                for (unsigned byte = 0U; byte < 4U; ++byte) {
                    value |= std::to_integer<std::uint32_t>(request.data[4U + byte]) << (8U * byte);
                }
                if (index == 0x60FFU && value != 0U) {
                    CHECK(sub == 2U && value == 5U);
                    CHECK(++nonzero == 1U);
                    moving = scenario != 10U;
                } else if (index == 0x60FFU && nonzero) {
                    zero_attempted = true;
                    if (sub == 2U && scenario == 6U) {
                        continue; // Zero is not acknowledged or applied; retain protection.
                    }
                    if (sub == 2U) {
                        moving = false;
                    }
                }
                if (index == 0x6040U) {
                    CHECK(!nonzero || value == 6U);
                    statusword = value == 15U ? 0x1427U : value == 7U ? 0x1423U : 0x1421U;
                }
                if (nonzero && !zero_attempted && value == 0U && (index == 0x1017U || index == 0x1800U)) {
                    suppression_seen = true;
                }
                if (nonzero && ((index == 0x1017U && value == 500U) || (index == 0x1800U && value == 100U))) {
                    CHECK(zero_attempted && !moving);
                }
                if (index == 0x2000U && value == 0U) {
                    CHECK(zero_attempted && !moving);
                    watchdog_restored = true;
                    if (scenario == 4U) {
                        continue;
                    }
                }
                values[key] = value;
                CHECK(peer.send(download_response(index, sub), 100ms).ok());
            } else {
                auto value = values[key];
                if (index == 0x606CU) {
                    value = moving && sub == 2U ? 5U : 0U;
                    if (nonzero && !zero_attempted && motion_probe_parts < 2U) {
                        CHECK(sub == ++motion_probe_parts);
                        CHECK(now - target_readback < 500ms);
                        motion_probe_completed = now;
                        if (scenario == 11U && sub == 1U) {
                            value = 5U;
                        }
                        if (scenario == 12U) {
                            continue; // Missing initial velocity response cannot qualify movement.
                        }
                    }
                }
                if (index == 0x60FFU && value == 5U) {
                    target_readback = now;
                }
                auto response = upload_response(index, sub, {0U, 0U, 0U, 0U});
                response.data[0] = index == 0x6060U || index == 0x6061U ? std::byte{0x4FU}
                                   : index == 0x1017U || index == 0x200FU || index == 0x2000U || index == 0x1800U
                                       ? std::byte{0x4BU}
                                       : std::byte{0x43U};
                for (unsigned byte = 0U; byte < 4U; ++byte) {
                    response.data[4U + byte] = std::byte{static_cast<std::uint8_t>(value >> (8U * byte))};
                }
                CHECK(peer.send(response, 100ms).ok());
            }
        }
    }};
    const auto result = session.qualify_communication_loss_cia402(stimulus);
    std::cout << "communication stimulus=" << static_cast<unsigned>(stimulus) << " scenario=" << scenario
              << " result=" << result.operation << ':' << result.context << '\n';
    CHECK(result.ok() == (scenario == 0U));
    if (scenario == 1U) {
        CHECK(result.operation == "qualification_nonzero_velocity");
    } else if (scenario == 2U) {
        CHECK(result.operation == "qualification_expected_feedback_loss_absent");
    } else if (scenario == 3U) {
        CHECK(result.operation == "qualification_unexpected_feedback_loss");
    } else if (scenario == 4U || scenario == 6U) {
        CHECK(result.operation == "CO_SDOclientDownload");
    } else if (scenario == 5U) {
        CHECK(result.operation == "qualification_watchdog_baseline");
    } else if (scenario == 7U) {
        CHECK(result.operation == "qualification_owner_exit");
    } else if (scenario == 8U) {
        CHECK(result.operation == "qualification_sequence_generation_or_bus_error");
    } else if (scenario == 9U) {
        CHECK(result.operation == "qualification_tpdo_timer_baseline");
    } else if (scenario == 10U) {
        CHECK(result.operation == "qualification_motion_not_observed");
    } else if (scenario == 11U) {
        CHECK(result.operation == "qualification_other_channel_velocity");
    } else if (scenario == 12U) {
        CHECK(result.error.value() == ETIMEDOUT);
    }
    if (scenario == 0U && !result.ok()) {
        std::cerr << result.operation << ':' << result.context << '\n';
    }
    responder.request_stop();
    responder.join();
    if (scenario >= 10U) {
        CHECK(!suppression_seen);
        CHECK(!stopped_by_watchdog);
    }
    if (scenario == 0U) {
        CHECK(motion_probe_parts == 2U);
    }
    CHECK(nonzero == (scenario == 5U || scenario == 9U ? 0U : 1U));
    CHECK(scenario == 5U || scenario == 9U || zero_attempted);
    if (scenario == 6U || scenario == 8U || scenario == 3U) {
        CHECK(!watchdog_restored);
        CHECK(result.context.find("watchdog_retained=") != std::string::npos);
    } else {
        CHECK(scenario == 5U || scenario == 9U || watchdog_restored);
    }
    CHECK(stimulus != CommunicationLossStimulus::watchdog || scenario != 0U || stopped_by_watchdog);
    CHECK(!session.qualify_communication_loss_cia402(stimulus).ok());
    expect_no_frame(peer);
}

/** Exercise manual TPDO mapping, rollback after every partial write, and no motor commands. */
void test_manual_tpdo(const std::string_view interface_name, CanSocket& peer, const unsigned scenario) {
    auto termination = TerminationEvent::create();
    auto owner = Lifecycle::create(config(interface_name), termination.value());
    CHECK(owner.ok());
    if (!owner.ok()) {
        return;
    }
    QualificationSession session{*owner.value()};
    std::map<std::pair<std::uint16_t, std::uint8_t>, std::uint32_t> values{
        {{0x1800U, 1U}, 0x181U},      {{0x1800U, 2U}, 255U},
        {{0x1800U, 5U}, 100U},        {{0x1A00U, 0U}, 2U},
        {{0x1A00U, 1U}, 0x60410020U}, {{0x1A00U, 2U}, 0x606C0320U},
        {{0x1017U, 0U}, 0U},          {{0x6041U, 0U}, scenario == 10U ? 0x14271427U : 0x14211421U},
        {{0x603FU, 0U}, 0U},          {{0x60FFU, 1U}, 0U},
        {{0x60FFU, 2U}, 0U},          {{0x606CU, 1U}, 0U},
        {{0x606CU, 2U}, 0U},          {{0x606CU, 3U}, 0U}};
    if (scenario == 11U) {
        values[{0x1800U, 5U}] = 101U;
    }
    unsigned writes = 0U;
    unsigned mapping_writes = 0U;
    unsigned starts = 0U;
    std::jthread responder{[&](const std::stop_token stop) {
        std::uint8_t nmt = 0x7FU;
        bool signal_sent = false;
        auto last_hb = std::chrono::steady_clock::now();
        auto last_tpdo = last_hb;
        while (!stop.stop_requested()) {
            const auto now = std::chrono::steady_clock::now();
            if (values[{0x1017U, 0U}] && now - last_hb >= 10ms) {
                CHECK(peer.send(frame(0x701U, {nmt}), 100ms).ok());
                last_hb = now;
            }
            if (nmt == 5U && now - last_tpdo >= 10ms && scenario != 12U) {
                CHECK(values[std::make_pair(std::uint16_t{0x1A00U}, std::uint8_t{1U})] == 0x606C0320U);
                const auto state = static_cast<std::uint8_t>(scenario == 14U ? 0x27U : 0x21U);
                CHECK(peer.send(frame(0x181U, {0U, 0U, 50U, 0U, state, 0x14U, state, 0x14U}), 100ms).ok());
                if (scenario == 13U && !signal_sent) {
                    CHECK(::kill(::getpid(), SIGTERM) == 0);
                    signal_sent = true;
                }
                last_tpdo = now;
            }
            const auto received = peer.receive(1ms);
            CHECK(received.ok());
            if (!received.ok() || !received.value()) {
                continue;
            }
            const auto request = received.value()->frame;
            if (request.raw_can_id == 0U) {
                CHECK(request.data[0] == std::byte{1U} || request.data[0] == std::byte{0x80U});
                nmt = request.data[0] == std::byte{1U} ? 5U : 0x7FU;
                if (nmt == 5U) {
                    ++starts;
                }
                continue;
            }
            const auto index = static_cast<std::uint16_t>(std::to_integer<unsigned>(request.data[1])
                                                          | std::to_integer<unsigned>(request.data[2]) << 8U);
            const auto sub = std::to_integer<std::uint8_t>(request.data[3]);
            const auto key = std::pair{index, sub};
            CHECK(values.contains(key));
            const auto size = index == 0x1017U || (index == 0x1800U && sub == 5U)                  ? 2U
                              : (index == 0x1800U && sub == 2U) || (index == 0x1A00U && sub == 0U) ? 1U
                                                                                                   : 4U;
            if (request.data[0] == std::byte{0x40U}) {
                auto value = values[key];
                if (nmt == 5U && index == 0x606CU) {
                    value = sub == 2U ? 50U : sub == 3U ? 50U << 16U : 0U;
                }
                auto response = upload_response(index, sub, {0U, 0U, 0U, 0U});
                response.data[0] = size == 1U ? std::byte{0x4FU} : size == 2U ? std::byte{0x4BU} : std::byte{0x43U};
                for (unsigned b = 0U; b < 4U; ++b) {
                    response.data[4U + b] = std::byte{static_cast<std::uint8_t>(value >> (8U * b))};
                }
                CHECK(peer.send(response, 100ms).ok());
            } else {
                ++writes;
                CHECK(index == 0x1017U || index == 0x1800U || index == 0x1A00U);
                std::uint32_t value = 0U;
                for (unsigned b = 0U; b < 4U; ++b) {
                    value |= std::to_integer<std::uint32_t>(request.data[4U + b]) << (8U * b);
                }
                values[key] = value;
                if (index != 0x1017U) {
                    ++mapping_writes;
                }
                if ((index != 0x1017U && mapping_writes == scenario && scenario <= 8U)
                    || (scenario == 15U && mapping_writes == 9U && index != 0x1017U)) {
                    continue; // Applied write with lost ACK: restoration must still be attempted.
                }
                CHECK(peer.send(download_response(index, sub), 100ms).ok());
            }
        }
    }};
    const auto result = session.capture_manual_tpdo(100ms);
    responder.request_stop();
    responder.join();
    std::cout << "manual_tpdo scenario=" << scenario << " result=" << result.operation << ':' << result.context << '\n';
    CHECK(result.ok() == (scenario == 0U));
    CHECK(starts == (scenario == 0U || scenario >= 12U ? 1U : 0U));
    if (scenario == 10U || scenario == 11U) {
        CHECK(writes == 0U);
    }
    CHECK(values[std::make_pair(std::uint16_t{0x1800U}, std::uint8_t{1U})] == (scenario == 15U ? 0x80000181U : 0x181U));
    CHECK(values[std::make_pair(std::uint16_t{0x1A00U}, std::uint8_t{0U})] == 2U);
    CHECK(values[std::make_pair(std::uint16_t{0x1A00U}, std::uint8_t{1U})]
          == (scenario == 15U ? 0x606C0320U : 0x60410020U));
    CHECK(values[std::make_pair(std::uint16_t{0x1A00U}, std::uint8_t{2U})]
          == (scenario == 15U ? 0x60410020U : 0x606C0320U));
    CHECK(values[std::make_pair(std::uint16_t{0x1017U}, std::uint8_t{0U})] == 0U);
    CHECK(!session.capture_manual_tpdo(100ms).ok());
    expect_no_frame(peer);
}

} // namespace

/** Run P6.3 qualification behavior against one namespace-local vcan bus. */
int main() {
    const char* const interface_name = std::getenv("ROBOT_CONTROL_TEST_VCAN_INTERFACE");
    if (interface_name == nullptr || !std::string_view{interface_name}.starts_with("vcan")) {
        std::cout << "SKIP: P6.3 requires the isolated managed-vcan runner\n";
        return 77;
    }
    const std::array filters{can_filter{.can_id = 0x000U, .can_mask = CAN_SFF_MASK},
                             can_filter{.can_id = 0x601U, .can_mask = CAN_SFF_MASK}};
    auto peer_result =
        CanSocket::open(interface_name, CanSocketConfig{.filters = std::span<const can_filter>{filters}});
    auto monitor_result = CanSocket::open(interface_name, CanSocketConfig{});
    CHECK(peer_result.ok());
    CHECK(monitor_result.ok());
    if (!peer_result.ok() || !monitor_result.ok()) {
        return 1;
    }
    auto peer = std::move(peer_result).value();
    auto monitor = std::move(monitor_result).value();
    // Block termination before spawning capture so signals stay with the owner signalfd.
    auto capture_signals = TerminationEvent::create();
    CHECK(capture_signals.ok());
    if (!capture_signals.ok()) {
        return 1;
    }
    std::atomic<std::size_t> monitored{0U};
    std::jthread capture{[&](const std::stop_token stop) {
        while (true) {
            const auto captured = monitor.receive(5ms);
            CHECK(captured.ok());
            if (!captured.ok()) {
                return;
            }
            if (captured.value().has_value()) {
                ++monitored;
                CHECK(monitor_allowed(captured.value()->frame));
            } else if (stop.stop_requested()) {
                return;
            }
        }
    }};
    for (unsigned scenario = 0U; scenario < 9U; ++scenario) {
        test_zero_sequence(interface_name, peer, scenario);
    }
    test_zero_preflight(interface_name, peer);
    test_first_motion(interface_name, peer, IndependentChannel::subindex_1);
    test_first_motion(interface_name, peer, IndependentChannel::subindex_2);
    test_nmt_stop_motion(interface_name, peer);
    test_controlword_stop_motion(interface_name, peer, TransitionControlword::shutdown, true);
    test_controlword_stop_motion(interface_name, peer, TransitionControlword::shutdown, false);
    test_controlword_stop_motion(interface_name, peer, TransitionControlword::disable_voltage, true);
    test_controlword_stop_motion(interface_name, peer, TransitionControlword::disable_voltage, false);
    test_controlword_stop_motion(interface_name, peer, TransitionControlword::quick_stop, true);
    test_controlword_stop_motion(interface_name, peer, TransitionControlword::quick_stop, false);
    test_controlword_stop_motion(interface_name, peer, TransitionControlword::quick_stop, true, 6U);
    test_nmt_stop_timeout_stays_preoperational(interface_name, peer);
    test_first_motion_failure_restores_application(interface_name, peer);
    test_target_requires_enabled(interface_name, peer);
    test_other_channel_motion_rejected(interface_name, peer);
    test_success(interface_name, peer);
    test_target_deadline_forces_zero(interface_name, peer);
    test_timeout(interface_name, peer);
    test_partial_sequence(interface_name, peer);
    test_feedback_loss(interface_name, peer);
    test_stale(interface_name, peer);
    test_zero_both_after_failure(interface_name, peer);
    for (const auto stimulus :
         {CommunicationLossStimulus::watchdog, CommunicationLossStimulus::heartbeat, CommunicationLossStimulus::tpdo}) {
        test_communication_loss(interface_name, peer, stimulus, 0U);
        test_communication_loss(interface_name, peer, stimulus, 4U);
        test_communication_loss(interface_name, peer, stimulus, 5U);
        test_communication_loss(interface_name, peer, stimulus, 6U);
        test_communication_loss(interface_name, peer, stimulus, 7U);
        test_communication_loss(interface_name, peer, stimulus, 8U);
        test_communication_loss(interface_name, peer, stimulus, 10U);
        test_communication_loss(interface_name, peer, stimulus, 11U);
        test_communication_loss(interface_name, peer, stimulus, 12U);
        if (stimulus == CommunicationLossStimulus::watchdog) {
            test_communication_loss(interface_name, peer, stimulus, 1U);
        } else {
            test_communication_loss(interface_name, peer, stimulus, 2U);
            test_communication_loss(interface_name, peer, stimulus, 3U);
        }
    }
    test_communication_loss(interface_name, peer, CommunicationLossStimulus::tpdo, 9U);
    for (const unsigned scenario : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 10U, 11U, 12U, 13U, 14U, 15U}) {
        test_manual_tpdo(interface_name, peer, scenario);
    }
    test_signal(interface_name, peer);
    capture.request_stop();
    capture.join();
    CHECK(monitored >= 400U);
    test_link_loss(interface_name, peer);
    std::cout << "INFO: P6.3/P6.4 managed-vcan frames=" << monitored.load()
              << " prohibited=0 failures=" << failures.load() << '\n';
    return failures.load() == 0 ? 0 : 1;
}
