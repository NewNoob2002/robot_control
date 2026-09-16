#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <linux/can/error.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include "communication/canopen/runtime.hpp"
#include "platform/linux/can/socket.hpp"
namespace {
using namespace std::chrono_literals;
using namespace robot_control;
using namespace communication::canopen;
using platform::linux::can::CanSocket;
using platform::linux::can::ClassicCanFrame;
using Clock = std::chrono::steady_clock;
int failures = 0, fail_send = 0;
unsigned send_calls = 0;
/** Check in all build variants without NDEBUG. */
void check(bool ok, const char* text) {
    if (!ok) {
        ++failures;
        std::cerr << text << '\n';
    }
}
#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__)
/** Independently encode peer frames. */
ClassicCanFrame frame(std::uint32_t id, std::initializer_list<unsigned> values) {
    ClassicCanFrame f{.raw_can_id = id, .payload_length = static_cast<std::uint8_t>(values.size())};
    std::size_t i = 0;
    for (auto v : values) {
        f.data[i++] = static_cast<std::byte>(v);
    }
    return f;
}
/** Software-only profile; timing margins are not hardware deployment values. */
domain::drive::RuntimeConfig config() {
    return {.left_target = domain::drive::PackedHalf::low,
            .left_feedback = domain::drive::PackedHalf::low,
            .left_sign = 1,
            .right_sign = 1,
            .max_abs_rpm = 100,
            .standstill_tenths_rpm = 0,
            .heartbeat_timeout = 300ms,
            .feedback_timeout = 150ms,
            .decision_timeout = 80ms};
}
/** Fixture readback, not evidence about physical configuration. */
RuntimeLayoutProof proof(Lifecycle& owner) {
    return {.generation = owner.observation_snapshot(Clock::now()).generation,
            .verified_at = Clock::now(),
            .rpdo_cob_id = 0x201,
            .status_cob_id = 0x181,
            .diagnostics_cob_id = 0x281,
            .rpdo_type = 255,
            .status_type = 255,
            .diagnostics_type = 255,
            .rpdo_map = {0x60400010, 0x60ff0320},
            .status_map = {0x60410020, 0x606c0320},
            .diagnostics_map = {0x60610008, 0x603f0020},
            .rpdo_count = 2,
            .status_count = 2,
            .diagnostics_count = 2,
            .command_application = 1};
}
/** Produce a current session/epoch-bound safety envelope. */
RuntimeSubmission decision(RuntimeSession& runtime, std::uint64_t sequence, std::uint64_t authorization,
                           std::int32_t left = 0, std::int32_t right = 0) {
    const auto state = runtime.state();
    const bool motion = left != 0 || right != 0;
    return {.request = {.decision = {.state = domain::safety::SafetyState::normal,
                                     .action = motion ? domain::safety::DriveAction::approved_target
                                                      : domain::safety::DriveAction::enable_operation,
                                     .approved_command = {left, right, false},
                                     .authorization_generation = authorization,
                                     .decision_generation = sequence,
                                     .motion_approved = motion},
                        .generation = state.feedback.generation,
                        .feedback_epoch = state.epoch,
                        .authorization = authorization,
                        .issued_at = Clock::now()},
            .session = runtime.session()};
}
/** Verify actual RPDO bytes observed by the independent peer. */
void expect(CanSocket& peer, std::initializer_list<unsigned> bytes) {
    const auto r = peer.receive(100ms);
    CHECK(r.ok() && r.value().has_value());
    if (r.ok() && r.value()) {
        const auto expected = frame(0x201, bytes);
        CHECK(r.value()->frame.raw_can_id == expected.raw_can_id);
        CHECK(r.value()->frame.payload_length == expected.payload_length);
        CHECK(r.value()->frame.data == expected.data);
    }
}
/** Check no retries or extra SDO/NMT requests appeared. */
void quiet(CanSocket& peer) {
    const auto r = peer.receive(0ms);
    CHECK(r.ok() && !r.value());
}
/** Run the real CANopen owner for a bounded receive interval. */
void pump(Lifecycle& owner) {
    const auto r = owner.run_until(Clock::now() + 5ms);
    CHECK(r.ok() && r.value() == LifecycleExit::deadline);
    if (!r.ok()) {
        std::cerr << r.status().operation << ' ' << r.status().error << '\n';
    }
}
/** Publish current heartbeat/status/speed/mode/fault observations. */
void healthy(Lifecycle& owner, CanSocket& peer) {
    CHECK(peer.send(frame(0x701, {5}), 10ms).ok());
    CHECK(peer.send(frame(0x181, {0x27, 0x14, 0x27, 0x14, 0, 0, 0, 0}), 10ms).ok());
    CHECK(peer.send(frame(0x281, {3, 0, 0, 0, 0}), 10ms).ok());
    pump(owner);
}
/** Arm only through zero output followed by a new zero observation. */
void arm(Lifecycle& owner, CanSocket& peer, RuntimeSession& runtime, std::uint64_t& sequence, std::uint64_t auth) {
    healthy(owner, peer);
    const auto r = runtime.submit(decision(runtime, ++sequence, auth));
    CHECK(r.ok() && r.value().accepted);
    expect(peer, {6, 0, 0, 0, 0, 0});
    healthy(owner, peer);
}
/** Exercise the production runtime against independent wire stimuli. */
int exercise(const std::string& name) {
    auto term = platform::linux::process::TerminationEvent::create();
    CHECK(term.ok());
    auto peer_result = CanSocket::open(name);
    CHECK(peer_result.ok());
    if (!term.ok() || !peer_result.ok()) {
        return 1;
    }
    auto peer = std::move(peer_result).value();
    StackConfig stack{.interface_name = name,
                      .controller_node_id = 127,
                      .remote_node_id = 1,
                      .heartbeat_timeout = 300ms,
                      .sdo_timeout = 100ms,
                      .tpdo_timeout = 150ms,
                      .tpdo_expected_dlc = {8, 5, 0, 0}};
    auto owner_result = Lifecycle::create(stack, term.value());
    CHECK(owner_result.ok());
    if (!owner_result.ok()) {
        return 1;
    }
    auto owner = std::move(owner_result).value();
    healthy(*owner, peer);
    quiet(peer);
    auto bad = proof(*owner);
    bad.command_application = 0;
    CHECK(!RuntimeSession::create(*owner, config(), bad).ok());
    bad = proof(*owner);
    bad.rpdo_count = 3;
    CHECK(!RuntimeSession::create(*owner, config(), bad).ok());
    bad = proof(*owner);
    bad.rpdo_map[1] = 0x60ff0120;
    CHECK(!RuntimeSession::create(*owner, config(), bad).ok());
    auto created = RuntimeSession::create(*owner, config(), proof(*owner));
    CHECK(created.ok());
    if (!created.ok()) {
        return 1;
    }
    auto runtime = std::move(created).value();
    CHECK(!RuntimeSession::create(*owner, config(), proof(*owner)).ok());
    quiet(peer);
    std::uint64_t seq = 0, auth = 1;
    arm(*owner, peer, *runtime, seq, auth);
    auto r = runtime->submit(decision(*runtime, ++seq, auth));
    CHECK(r.ok() && r.value().accepted);
    expect(peer, {15, 0, 0, 0, 0, 0});
    long long maximum_us = 0;
    for (int i = 0; i < 10; ++i) {
        healthy(*owner, peer);
        const auto before = Clock::now();
        r = runtime->submit(decision(*runtime, ++seq, auth, 5, -6));
        CHECK(r.ok() && r.value().accepted);
        expect(peer, {15, 0, 5, 0, 250, 255});
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - before).count();
        maximum_us = std::max(maximum_us, static_cast<long long>(elapsed));
    }
    CHECK(maximum_us < 100000);
    std::cout << "runtime submission-to-peer max_us=" << maximum_us << " samples=10\n";
    for (const auto& fault : {frame(0x181, {8, 0x14, 0x27, 0x14, 0, 0, 0, 0}), frame(0x281, {1, 0, 0, 0, 0}),
                              frame(0x281, {3, 0, 0, 1, 0}), frame(0x181, {0x27}), frame(0x701, {0x7f})}) {
        const auto old = decision(*runtime, seq + 1, auth, 5, 0);
        CHECK(peer.send(fault, 10ms).ok());
        healthy(*owner, peer);
        expect(peer, {6, 0, 0, 0, 0, 0});
        CHECK(!runtime->state().armed);
        r = runtime->submit(old);
        ++seq;
        CHECK(r.ok() && !r.value().accepted);
        expect(peer, {6, 0, 0, 0, 0, 0});
        arm(*owner, peer, *runtime, seq, ++auth);
        r = runtime->submit(decision(*runtime, ++seq, auth, 5, 0));
        CHECK(r.ok() && r.value().accepted);
        expect(peer, {15, 0, 5, 0, 0, 0});
    }
    auto deadline = owner->run_until(Clock::now() + 90ms);
    CHECK(deadline.ok());
    CHECK(!runtime->state().armed);
    expect(peer, {6, 0, 0, 0, 0, 0});
    quiet(peer);
    arm(*owner, peer, *runtime, seq, ++auth);
    fail_send = EIO;
    const auto calls = send_calls;
    r = runtime->submit(decision(*runtime, ++seq, auth, 5, 0));
    CHECK(!r.ok());
    CHECK(r.status().operation == "runtime_rpdo_send" && r.status().error.value() == EIO);
    CHECK(send_calls == calls + 1 && !runtime->state().armed);
    CHECK(runtime->state().stopped);
    CHECK(!runtime->stop().ok());
    CHECK(!runtime->stop().ok());
    CHECK(send_calls == calls + 1);
    quiet(peer);
    runtime.reset();
    healthy(*owner, peer);
    created = RuntimeSession::create(*owner, config(), proof(*owner));
    CHECK(created.ok());
    if (!created.ok()) {
        return 1;
    }
    runtime = std::move(created).value();
    seq = 0;
    auth = 1;
    arm(*owner, peer, *runtime, seq, auth);
    r = runtime->submit(decision(*runtime, ++seq, auth, 5, 0));
    CHECK(r.ok());
    expect(peer, {15, 0, 5, 0, 0, 0});
    CHECK(::kill(::getpid(), SIGTERM) == 0);
    const auto signal_exit = owner->run_until(Clock::now() + 100ms);
    CHECK(signal_exit.ok() && signal_exit.value() == LifecycleExit::sigterm);
    CHECK(runtime->state().stopped);
    expect(peer, {6, 0, 0, 0, 0, 0});
    CHECK(runtime->stop().ok());
    quiet(peer);
    const auto old_session = runtime->session();
    runtime.reset();
    healthy(*owner, peer);
    created = RuntimeSession::create(*owner, config(), proof(*owner));
    CHECK(created.ok());
    if (!created.ok()) {
        return 1;
    }
    runtime = std::move(created).value();
    auto stale = decision(*runtime, 1, 1);
    stale.session = old_session;
    CHECK(!runtime->submit(stale).ok());
    expect(peer, {6, 0, 0, 0, 0, 0});
    seq = 1;
    auth = 1;
    arm(*owner, peer, *runtime, seq, auth);
    CHECK(peer.send(frame(0x701, {0}), 10ms).ok());
    healthy(*owner, peer);
    CHECK(runtime->state().stopped);
    quiet(peer);
    CHECK(!runtime->submit(decision(*runtime, ++seq, ++auth, 5, 0)).ok());
    quiet(peer);
    CHECK(!runtime->stop().ok());
    CHECK(!runtime->stop().ok());
    quiet(peer);
    runtime.reset();
    for (int missing = 0; missing < 3; ++missing) {
        healthy(*owner, peer);
        auto slow = config();
        slow.decision_timeout = 1s;
        created = RuntimeSession::create(*owner, slow, proof(*owner));
        CHECK(created.ok());
        if (!created.ok()) {
            return 1;
        }
        runtime = std::move(created).value();
        seq = 0;
        auth = 1;
        arm(*owner, peer, *runtime, seq, auth);
        r = runtime->submit(decision(*runtime, ++seq, auth, 5, 0));
        CHECK(r.ok() && r.value().accepted);
        expect(peer, {15, 0, 5, 0, 0, 0});
        const auto end = Clock::now() + 400ms;
        while (runtime->state().armed && Clock::now() < end) {
            if (missing != 0) {
                CHECK(peer.send(frame(0x701, {5}), 10ms).ok());
            }
            if (missing != 1) {
                CHECK(peer.send(frame(0x181, {0x27, 0x14, 0x27, 0x14, 0, 0, 0, 0}), 10ms).ok());
            }
            if (missing != 2) {
                CHECK(peer.send(frame(0x281, {3, 0, 0, 0, 0}), 10ms).ok());
            }
            pump(*owner);
        }
        CHECK(!runtime->state().armed);
        expect(peer, {6, 0, 0, 0, 0, 0});
        quiet(peer);
        CHECK(runtime->stop().ok());
        expect(peer, {6, 0, 0, 0, 0, 0});
        runtime.reset();
    }
    healthy(*owner, peer);
    created = RuntimeSession::create(*owner, config(), proof(*owner));
    CHECK(created.ok());
    if (!created.ok()) {
        return 1;
    }
    runtime = std::move(created).value();
    seq = 0;
    auth = 1;
    arm(*owner, peer, *runtime, seq, auth);
    r = runtime->submit(decision(*runtime, ++seq, auth, 5, 0));
    CHECK(r.ok() && r.value().accepted);
    expect(peer, {15, 0, 5, 0, 0, 0});
    can_frame error{};
    error.can_id = CAN_ERR_FLAG | CAN_ERR_BUSOFF;
    error.can_dlc = CAN_ERR_DLC;
    CHECK(::write(peer.fd(), &error, CAN_MTU) == CAN_MTU);
    pump(*owner);
    CHECK(runtime->state().stopped);
    quiet(peer);
    CHECK(runtime->state().output.reason == domain::drive::RuntimeReason::generation_changed);
    CHECK(!runtime->submit(decision(*runtime, ++seq, ++auth)).ok());
    quiet(peer);
    runtime.reset();
    healthy(*owner, peer);
    created = RuntimeSession::create(*owner, config(), proof(*owner));
    CHECK(created.ok());
    if (!created.ok()) {
        return 1;
    }
    runtime = std::move(created).value();
    seq = 0;
    auth = 1;
    arm(*owner, peer, *runtime, seq, auth);
    fail_send = -1;
    r = runtime->submit(decision(*runtime, ++seq, auth, 5, 0));
    CHECK(!r.ok() && r.status().error.value() == EIO);
    CHECK(runtime->state().stopped);
    quiet(peer);
    return failures == 0 ? 0 : 1;
}
} // namespace
// NOLINTNEXTLINE(bugprone-reserved-identifier): GNU ld --wrap ABI.
extern "C" ssize_t __real_send(int, const void*, std::size_t, int);
/** Inject one RPDO send failure, leaving peer traffic unchanged. */
// NOLINTNEXTLINE(bugprone-reserved-identifier): GNU ld --wrap ABI.
extern "C" ssize_t __wrap_send(int fd, const void* data, std::size_t size, int flags) {
    const auto* f = static_cast<const can_frame*>(data);
    if (size == CAN_MTU && f->can_id == 0x201) {
        ++send_calls;
        if (fail_send == -1) {
            fail_send = 0;
            errno = 0;
            return CAN_MTU - 1;
        }
        if (fail_send) {
            errno = fail_send;
            fail_send = 0;
            return -1;
        }
    }
    return __real_send(fd, data, size, flags);
}
/** Run only on the managed test namespace's vcan0. */
int main() {
    const char* name = std::getenv("ROBOT_CONTROL_TEST_VCAN_INTERFACE");
    if (!name || std::string{name} != "vcan0") {
        std::cerr << "managed vcan0 required\n";
        return 77;
    }
    return exercise(name);
}
