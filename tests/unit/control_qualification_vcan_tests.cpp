#include "communication/canopen/control_qualification.hpp"
#include "platform/linux/can/socket.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <map>
#include <thread>
#include <unistd.h>

namespace {
using namespace std::chrono_literals;
using namespace robot_control;
using namespace communication::canopen;
using platform::linux::can::CanSocket;
using platform::linux::can::ClassicCanFrame;
std::atomic<int> failures{0};
/** Retain assertion failures in both debug and release configurations. */
void check(bool ok, int line) {
    if (!ok) {
        ++failures;
        std::cerr << "layout assertion line=" << line << '\n';
    }
}
#define CHECK(x) check((x), __LINE__)
/** Encode a separate peer frame without production protocol helpers. */
ClassicCanFrame frame(unsigned id, std::initializer_list<unsigned> bytes) {
    ClassicCanFrame f{.raw_can_id = id, .payload_length = static_cast<std::uint8_t>(bytes.size())};
    std::size_t i = 0;
    for (auto b : bytes)
        f.data[i++] = static_cast<std::byte>(b);
    return f;
}
/** Cover exact baseline, malformed diagnostics, partial writes, restoration and signal abort. */
void trial(unsigned scenario) {
    auto term = platform::linux::process::TerminationEvent::create();
    auto peer_result = CanSocket::open("vcan0");
    CHECK(term.ok() && peer_result.ok());
    if (!term.ok() || !peer_result.ok())
        return;
    auto peer = std::move(peer_result).value();
    StackConfig cfg{.interface_name = "vcan0",
                    .controller_node_id = 127,
                    .remote_node_id = 1,
                    .heartbeat_timeout = 100ms,
                    .sdo_timeout = 30ms,
                    .tpdo_timeout = 50ms,
                    .tpdo_expected_dlc = {8, 5, 0, 0}};
    auto created = Lifecycle::create(cfg, term.value());
    CHECK(created.ok());
    if (!created.ok())
        return;
    auto owner = std::move(created).value();
    ControlQualification session{*owner};
    using Key = std::pair<std::uint16_t, std::uint8_t>;
    std::map<Key, std::uint32_t> values{
        {{0x6040, 0}, 0},          {{0x6041, 0}, 0x00400040}, {{0x6060, 0}, 3},   {{0x6061, 0}, 3},
        {{0x603f, 0}, 0},          {{0x200f, 0}, 1},          {{0x2000, 0}, 0},   {{0x1400, 1}, 0x201},
        {{0x1400, 2}, 255},        {{0x1400, 5}, 1000},       {{0x1600, 0}, 2},   {{0x1600, 1}, 0x60400010},
        {{0x1600, 2}, 0x60600008}, {{0x60ff, 1}, 0},          {{0x60ff, 2}, 0},   {{0x606c, 1}, 0},
        {{0x606c, 2}, 0},          {{0x606c, 3}, 0},          {{0x1017, 0}, 0},   {{0x1800, 1}, 0x181},
        {{0x1800, 2}, 255},        {{0x1800, 5}, 100},        {{0x1a00, 0}, 2},   {{0x1a00, 1}, 0x60410020},
        {{0x1a00, 2}, 0x606c0320}, {{0x1801, 1}, 0x281},      {{0x1801, 2}, 255}, {{0x1801, 5}, 0},
        {{0x1a01, 0}, 0},          {{0x1a01, 1}, 0},          {{0x1a01, 2}, 0}};
    if (scenario == 1)
        values[{0x1a01, 1}] = 1; // A foreign inactive descriptor must not be overwritten.
    if (scenario == 2)
        values[{0x6041, 0}] = 0x00270027;
    if (scenario >= 29 && scenario <= 31) {
        values[{0x606c, 1}] = 0xfffffff6U; // -1rpm independent signed32.
        values[{0x606c, 2}] = scenario == 30 ? 11U : 10U;
        values[{0x606c, 3}] = scenario == 31 ? 0x000bfff6U : 0x000afff6U;
    }
    if (scenario >= 37 && scenario <= 39) {
        values[{0x606c, 1}] = 0xfffffff1U; // Signed -1.5rpm.
        values[{0x606c, 2}] = scenario == 38 ? 16U : 15U;
        values[{0x606c, 3}] = scenario == 39 ? 0x0010fff1U : 0x000ffff1U;
    }
    if (scenario >= 40) {
        values[{0x606c, 1}] = 0xffffffecU; // Signed -2rpm.
        values[{0x606c, 2}] = scenario == 41 ? 21U : 20U;
        values[{0x606c, 3}] = scenario == 42 ? 0x0015ffecU : 0x0014ffecU;
    }
    const auto clean_baseline = values;
    if (scenario >= 32 && scenario <= 36) {
        for (const auto& [key, value] : std::map<Key, std::uint32_t>{{{0x605a, 0}, 5},
                                                                     {{0x2000, 0}, 1000},
                                                                     {{0x1017, 0}, 500},
                                                                     {{0x1600, 2}, 0x60ff0320},
                                                                     {{0x1801, 5}, 100},
                                                                     {{0x1a01, 0}, 2},
                                                                     {{0x1a01, 1}, 0x60610008},
                                                                     {{0x1a01, 2}, 0x603f0020}})
            values[key] = value;
        if (scenario == 33)
            values[{0x1600, 2}] = 0x12345678;
        if (scenario == 34)
            values[{0x6041, 0}] = 0x00270027;
        if (scenario == 35)
            values[{0x606c, 2}] = 11;
        if (scenario == 36)
            values[{0x60ff, 1}] = 1;
    }
    const auto initial = values;
    unsigned writes = 0;
    unsigned starts = 0;
    std::jthread responder{[&](const std::stop_token& stop) {
        unsigned nmt = scenario >= 32 ? 5U : 127U;
        bool signaled = false;
        auto next = std::chrono::steady_clock::now();
        while (!stop.stop_requested()) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= next) {
                next = now + 5ms;
                if (values[{0x1017, 0}] != 0)
                    CHECK(peer.send(frame(0x701, {nmt}), 10ms).ok());
                if (nmt == 5) {
                    CHECK(peer.send(frame(0x181, {values[{0x6041, 0}] & 255U, 0, (values[{0x6041, 0}] >> 16U) & 255U, 0,
                                                  scenario == 9 ? 1U : 0U, 0, 0, 0}),
                                    10ms)
                              .ok());
                    if (scenario != 3)
                        CHECK(peer.send(frame(0x281, {scenario == 4 ? 2U : 3U, 0, 0, scenario == 5 ? 1U : 0U, 0}), 10ms)
                                  .ok());
                    if (scenario == 8 && !signaled) {
                        CHECK(peer.send(frame(0x701, {0}), 10ms).ok());
                        signaled = true;
                    }
                    if (scenario == 6 && !signaled) {
                        CHECK(::kill(::getpid(), SIGTERM) == 0);
                        signaled = true;
                    }
                }
            }
            auto received = peer.receive(1ms);
            CHECK(received.ok());
            if (!received.ok())
                break;
            const auto& observation = received.value();
            if (!observation.has_value())
                continue;
            const auto f = observation->frame;
            if (f.raw_can_id == 0) {
                CHECK(f.payload_length == 2 && f.data[1] == std::byte{1});
                CHECK(f.data[0] == std::byte{1} || f.data[0] == std::byte{0x80});
                nmt = f.data[0] == std::byte{1} ? 5 : 127;
                if (nmt == 5)
                    ++starts;
                continue;
            }
            CHECK(f.raw_can_id == 0x601 && f.payload_length == 8);
            const auto index = static_cast<std::uint16_t>(std::to_integer<unsigned>(f.data[1])
                                                          | (std::to_integer<unsigned>(f.data[2]) << 8U));
            const auto sub = std::to_integer<std::uint8_t>(f.data[3]);
            const Key key{index, sub};
            CHECK(values.contains(key));
            auto response = frame(
                0x581, {0, static_cast<unsigned>(index & 255U), static_cast<unsigned>(index >> 8U), sub, 0, 0, 0, 0});
            if (f.data[0] == std::byte{0x40}) {
                const auto width = index == 0x1017 || index == 0x605a || index == 0x6040 || index == 0x2000
                                           || index == 0x200f
                                           || ((index == 0x1400 || index == 0x1800 || index == 0x1801) && sub == 5)
                                       ? 2U
                                   : index == 0x6060 || index == 0x6061
                                           || ((index == 0x1400 || index == 0x1800 || index == 0x1801) && sub == 2)
                                           || ((index == 0x1600 || index == 0x1a00 || index == 0x1a01) && sub == 0)
                                       ? 1U
                                       : 4U;
                response.data[0] = width == 1 ? std::byte{0x4f} : width == 2 ? std::byte{0x4b} : std::byte{0x43};
                for (unsigned i = 0; i < 4; ++i)
                    response.data[4 + i] = static_cast<std::byte>(values[key] >> (8U * i));
            } else {
                CHECK(index == 0x1017 || index == 0x2000 || index == 0x1400 || index == 0x1600 || index == 0x1801
                      || index == 0x1a01 || index == 0x60ff || index == 0x6040);
                if (index == 0x1400 || index == 0x1600 || index == 0x1801 || index == 0x1a01)
                    CHECK(nmt == 127);
                ++writes;
                std::uint32_t value = 0;
                for (unsigned i = 0; i < 4; ++i)
                    value |= std::to_integer<std::uint32_t>(f.data[4 + i]) << (8U * i);
                if (index == 0x60ff)
                    CHECK(value == 0);
                if (index == 0x6040) {
                    CHECK(value == 0 || value == 6);
                    values[{0x6041, 0}] = value == 0 ? 0x00400040 : 0x00210021;
                }
                values[key] = value;
                response.data[0] = std::byte{0x60};
                // Apply each setup write but lose its ACK: cleanup must restore every partial stage.
                if (scenario >= 10 && scenario <= 27 && writes == scenario - 9)
                    continue;
                // Loss of the first restore acknowledgement must remain a failure.
                if (scenario == 7 && starts && index == 0x1801 && sub == 1 && value == 0x80000281U)
                    continue;
            }
            CHECK(peer.send(response, 10ms).ok());
        }
    }};
    if (scenario >= 32 && scenario <= 36) {
        const auto restored = session.restore_interrupted(10);
        responder.request_stop();
        responder.join();
        CHECK(restored.ok() == (scenario == 32));
        if (scenario == 32) {
            CHECK(writes == 18 && starts == 0);
            for (const auto& [key, value] : clean_baseline)
                CHECK(values[key] == value);
        } else {
            CHECK(writes == 0 && starts == 0 && values == initial);
        }
        CHECK(!session.restore_interrupted(10).ok());
        return;
    }
    const auto result = session.prepare(false, scenario >= 40 ? 20 : scenario >= 37 ? 15 : scenario >= 29 ? 10 : 0);
    CHECK(result.ok() == (scenario == 0 || scenario == 7 || scenario == 28 || scenario == 29 || scenario == 37 || scenario == 40));
    if (scenario == 28 && result.ok()) {
        const domain::drive::RuntimeConfig config{
            domain::drive::PackedHalf::low, domain::drive::PackedHalf::low, 1, 1, 5, 0, 100ms, 50ms, 20ms};
        auto runtime = RuntimeSession::create(*owner, config, result.value());
        CHECK(runtime.ok());
        CHECK(!session.finish().ok()); // No SDO while an RPDO writer is attached.
        if (runtime.ok())
            runtime.value().reset();
    }
    const auto cleanup = session.finish();
    CHECK(session.finish().ok() == cleanup.ok());
    responder.request_stop();
    responder.join();
    std::cout << "scenario=" << scenario << " prepare=" << result.status().operation << " cleanup=" << cleanup.operation
              << ':' << cleanup.context << std::endl;
    if (scenario != 7 && scenario != 8 && scenario != 9) {
        CHECK(cleanup.ok());
        CHECK(values == initial);
    }
    if (scenario == 7 || scenario == 8 || scenario == 9)
        CHECK(!cleanup.ok());
    if (scenario == 1 || scenario == 2 || scenario == 30 || scenario == 31 || scenario == 38 || scenario == 39 || scenario == 41 || scenario == 42)
        CHECK(writes == 0 && starts == 0);
    CHECK(!session.prepare().ok());
}
} // namespace
/** Run only inside the managed network namespace; never on physical CAN. */
int main() {
    const auto* name = std::getenv("ROBOT_CONTROL_TEST_VCAN_INTERFACE");
    if (name == nullptr || std::string{name} != "vcan0")
        return 77;
    for (unsigned scenario = 0; scenario <= 42; ++scenario)
        trial(scenario);
    return failures.load() == 0 ? 0 : 1;
}
