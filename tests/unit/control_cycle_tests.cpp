#include "application/control/control_cycle.hpp"
#include "input/sbus/source/source.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
using namespace std::chrono_literals;
using namespace robot_control;
using namespace domain;
using application::control::ControlCycle;
using application::control::CycleConfig;
using application::control::CycleInput;
using application::control::CycleResult;
int failures = 0;
/** Record an independent contract assertion, including in Release builds. */
void check(bool ok, const char* label) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL " << label << '\n';
    }
}
/** Build an explicit virtual binding, never a physical deployment default. */
CycleConfig config() {
    CycleConfig c;
    c.runtime = {drive::PackedHalf::low, drive::PackedHalf::low, 1, 1, 100, 0, 100ms, 100ms, 30ms};
    c.arbiter.handover_zero_dwell = 20ms;
    c.arbiter.max_abs_rpm = 100;
    return c;
}
/** Construct one immutable enabled source sample. */
command::CommandSample sample(command::Source source, time::MonotonicTime now, std::uint64_t seq,
                              std::uint64_t auth = 1, int rpm = 0) {
    return {source, {rpm, rpm, false}, now, 1, auth, seq, true, true, true, false, false};
}
/** Fake-clock harness with explicit source and raw drive observations. */
struct Rig {
    ControlCycle cycle{config()};
    time::MonotonicTime now{};
    std::uint64_t sequence{0};
    CycleInput input{.coherent = true, .emergency_stop_known = true};
    drive::RuntimeFeedback feedback{
        .generation = {1, 0}, .status_raw = 0x00400040, .mode_raw = 3, .current = true, .operational = true};
    /** Advance by one nominal cycle and supply genuinely new feedback. */
    // Positional fixture values intentionally mirror RPM, authorization and raw status.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    CycleResult step(int rpm = 0, std::uint64_t auth = 1, std::uint32_t status = 0x00400040) {
        now += 10ms;
        ++sequence;
        input.sbus = sample(command::Source::sbus, now, sequence, auth, rpm);
        feedback.version = sequence;
        feedback.heartbeat_at = feedback.status_at = feedback.diagnostics_at = now;
        feedback.status_raw = status;
        cycle.observe(feedback, now);
        return cycle.tick(input, now);
    }
    /** Observe the zero hold and each drive transition before requesting motion. */
    void arm(std::uint64_t auth = 1) {
        check(step(0, auth).output.command.is_zero(), "startup zero");
        check(step(0, auth).output.command.is_zero(), "dwell zero");
        auto r = step(0, auth);
        check(r.output.accepted && r.output.payload[0] == std::byte{6}, "new authorization sends shutdown");
        r = step(0, auth, 0x00210021);
        check(r.output.payload[0] == std::byte{7}, "ready then switch on");
        r = step(0, auth, 0x00230023);
        check(r.output.payload[0] == std::byte{15}, "switched then enable");
        check(step(0, auth, 0x00270027).output.command.is_zero(), "enabled neutral");
        r = step(35, auth, 0x00270027);
        check(r.output.accepted && r.output.command.left_rpm == 35, "SBUS alone motion");
    }
};
/** Exercise fault, recovery and startup/shutdown end-to-end guards. */
void pipeline() {
    Rig r;
    r.arm();
    check(r.step(40, 1, 0x00270027).output.command.left_rpm == 40, "continuous authorization");
    check(r.step(0, 1, 0x00270027).output.command.is_zero(), "neutral deceleration");
    check(r.step(40, 1, 0x00270027).output.command.left_rpm == 40, "neutral retains owner");
    r.feedback.fault_raw = 0x00010000;
    const auto fault = r.step(40, 1, 0x00270027);
    check(fault.output.command.is_zero() && fault.request.decision.state == safety::SafetyState::drive_fault,
          "one axis fault inhibits both");
    r.feedback.fault_raw = 0;
    check(r.step(40, 1, 0x00270027).output.command.is_zero(), "recovery cannot reuse authorization");
    check(r.step(0, 1).output.command.is_zero(), "neutral alone cannot rearm");
    r.arm(2);
    r.input.emergency_stop_active = true;
    check(r.step(30, 2, 0x00270027).output.command.is_zero(), "emergency stop");
    r.input.emergency_stop_active = false;
    check(r.step(30, 2, 0x00270027).output.command.is_zero(), "emergency recovery requires rearm");
    r.input.shutdown_requested = true;
    auto result = r.step();
    check(result.request.decision.state == safety::SafetyState::shutdown, "shutdown decision");
    r.input.shutdown_requested = false;
    check(r.step(50, 9, 0x00270027).output.command.is_zero(), "shutdown terminal");

    Rig nonneutral;
    check(nonneutral.step(50).output.command.is_zero(), "startup deflected");
    for (int i = 0; i < 5; ++i)
        check(nonneutral.step().output.command.is_zero(), "deflected authorization consumed");
    nonneutral.arm(2);
}
/** Check precise deadlines, stale/replayed snapshots, events and decision replay. */
void invalidation() {
    Rig r;
    r.arm();
    const auto last = r.step(30, 1, 0x00270027);
    auto replay_guard = drive::RuntimePolicy{config().runtime};
    replay_guard.observe(r.feedback, r.now);
    check(!replay_guard.evaluate(last.request, r.now + 30ms).accepted, "expired envelope rejected");
    r.now += 100ms;
    auto result = r.cycle.tick(r.input, r.now);
    check(result.output.command.is_zero(), "exact feedback/source deadline");
    check(r.step(30, 1, 0x00270027).output.command.is_zero(), "gap cannot auto rearm");

    Rig transient;
    transient.arm();
    transient.feedback.fault_raw = 1;
    transient.cycle.observe(transient.feedback, transient.now);
    transient.feedback.fault_raw = 0;
    transient.cycle.observe(transient.feedback, transient.now);
    check(transient.step(30, 1, 0x00270027).output.command.is_zero(), "fault then healthy between cycles");

    Rig replay;
    replay.arm();
    const auto old = replay.input;
    static_cast<void>(replay.step(40, 1, 0x00270027));
    check(replay.cycle.tick(old, replay.now).output.command.is_zero(), "old source sequence");
    check(replay.step(40, 1, 0x00270027).output.command.is_zero(), "replay revokes authority");

    Rig boot;
    boot.arm();
    ++boot.feedback.generation.boot;
    check(boot.step(40, 1, 0x00270027).output.command.is_zero(), "boot invalidates owner");
    Rig clock;
    clock.arm();
    check(clock.cycle.tick(clock.input, clock.now - 1ms).output.command.is_zero(), "clock regression");
    check(clock.step(30, 10, 0x00270027).output.command.is_zero(), "clock regression terminal");
}
/** Verify source handover, generation changes and bounded transition waits. */
void handover_and_boundaries() {
    Rig r;
    r.arm();
    const auto external_step = [&](int rpm, std::uint32_t status) {
        r.input.external = sample(command::Source::external, r.now + 10ms, r.sequence + 1, 1, rpm);
        return r.step(0, 1, status);
    };
    for (int i = 0; i < 5; ++i) {
        const auto result = external_step(0, 0x00270027);
        check(result.output.command.is_zero(), "handover holds zero");
    }
    check(external_step(0, 0x00210021).output.payload[0] == std::byte{7}, "external switch on");
    check(external_step(0, 0x00230023).output.payload[0] == std::byte{15}, "external enable");
    static_cast<void>(external_step(0, 0x00270027));
    auto result = external_step(55, 0x00270027);
    check(result.selected.source == command::Source::external && result.output.command.left_rpm == 55,
          "external owns after zero and new system generation");
    check(external_step(55, 0x00270027).output.command.left_rpm == 55, "external continuous output");
    result = r.step(30, 1, 0x00270027);
    check(result.selected.source == command::Source::sbus && result.output.command.is_zero(),
          "manual takeover cannot replay consumed SBUS authorization");
    r.input.external = {};
    r.arm(2);

    Rig interlock;
    interlock.arm();
    for (int i = 0; i < 9; ++i) {
        interlock.input.external =
            sample(command::Source::external, interlock.now + 10ms, interlock.sequence + 1, 1, i == 8 ? 55 : 0);
        const auto status = i < 5 ? 0x00270027U : (i == 5 ? 0x00210021U : (i == 6 ? 0x00230023U : 0x00270027U));
        const auto ext = interlock.step(0, 1, status);
        if (i == 8)
            check(ext.output.command.left_rpm == 55, "external interlock setup");
    }
    interlock.input.sbus.authorization_generation = 2;
    ++interlock.input.sbus.sequence;
    check(interlock.cycle.tick(interlock.input, interlock.now).output.command.is_zero(),
          "new SBUS interlock generation revokes active external source");

    Rig deadline;
    static_cast<void>(deadline.step());
    static_cast<void>(deadline.step());
    static_cast<void>(deadline.step());
    for (int i = 0; i < 49; ++i)
        check(deadline.step().output.command.is_zero(), "waiting transition stays zero");
    check(deadline.step().output.payload[0] == std::byte{2}, "transition exact timeout quick stop");
    check(deadline.step(30, 1, 0x00270027).output.command.is_zero(), "late transition cannot revive authority");

    Rig stale_source;
    stale_source.arm();
    stale_source.now += 10ms;
    stale_source.input.sbus.captured_at = stale_source.now - 100ms;
    check(stale_source.cycle.tick(stale_source.input, stale_source.now).output.command.is_zero(),
          "source exact timeout");

    Rig mutation;
    mutation.arm();
    mutation.input.sbus.command.left_rpm = 99;
    check(mutation.cycle.tick(mutation.input, mutation.now).output.command.is_zero(), "same sequence mutation");

    Rig session;
    session.arm();
    session.input.sbus.session_generation = 0;
    check(session.cycle.tick(session.input, session.now).output.command.is_zero(), "source old session");

    Rig no_new_status;
    static_cast<void>(no_new_status.step());
    static_cast<void>(no_new_status.step());
    static_cast<void>(no_new_status.step());
    no_new_status.now += 10ms;
    no_new_status.feedback.status_raw = 0x00210021;
    no_new_status.cycle.observe(no_new_status.feedback, no_new_status.now);
    const auto waiting = no_new_status.cycle.tick(no_new_status.input, no_new_status.now);
    check(waiting.output.accepted && waiting.output.payload[0] == std::byte{6}, "new status requires new timestamp");

    auto bad = config();
    bad.period = 0ms;
    ControlCycle invalid{bad};
    check(!invalid.tick({}, {}).output.accepted, "invalid configuration");
}

/** Invalidate each independent observation/input channel while actually moving. */
void independent_losses() {
    for (int fault = 0; fault < 9; ++fault) {
        Rig r;
        r.arm();
        auto input = r.input;
        auto feedback = r.feedback;
        r.now += 10ms;
        switch (fault) {
            case 0:
                input.sbus.lost = true;
                break;
            case 1:
                input.sbus.failsafe = true;
                break;
            case 2:
                input.emergency_stop_known = false;
                break;
            case 3:
                input.coherent = false;
                break;
            case 4:
                feedback.current = false;
                break;
            case 5:
                feedback.mode_raw = 2;
                break;
            case 6:
                feedback.heartbeat_at = r.now - 100ms;
                break;
            case 7:
                feedback.status_at = r.now - 100ms;
                break;
            case 8:
                feedback.diagnostics_at = r.now - 100ms;
                break;
            default:
                check(false, "invalid fault fixture");
                break;
        }
        r.cycle.observe(feedback, r.now);
        check(r.cycle.tick(input, r.now).output.command.is_zero(), "independent input or feedback loss");
        check(r.step(35, 1, 0x00270027).output.command.is_zero(), "loss cannot automatically restore motion");
    }
}

/** Drive actual SBUS source snapshots through the cycle without an external source. */
void sbus_snapshots() {
    input::sbus::SourceConfig c;
    c.steering = {200, 1000, 1800, false};
    c.throttle = c.steering;
    c.recovery_frames = 1;
    input::sbus::Source source{c};
    Rig r;
    input::sbus::protocol::ParseResult event{};
    event.kind = input::sbus::protocol::EventKind::frame;
    event.frame.channels.fill(1000);
    event.frame.channels[5] = 200;
    auto now = time::MonotonicTime{1ms};
    static_cast<void>(source.consume({std::span{&event, 1}, now, 1}, now));
    event.frame.channels[5] = 1800;
    now += 10ms;
    auto s = source.consume({std::span{&event, 1}, now, 1}, now);
    check(s.sample.valid && s.sample.command.is_zero(), "real source neutral enable");
    for (int i = 0; i < 7; ++i) {
        now += 10ms;
        event.frame.channels[2] = i >= 6 ? 1800 : 1000;
        s = source.consume({std::span{&event, 1}, now, 1}, now);
        r.input.sbus = s.sample;
        r.feedback.version = static_cast<std::uint64_t>(i) + 1U;
        r.feedback.heartbeat_at = r.feedback.status_at = r.feedback.diagnostics_at = now;
        r.feedback.status_raw = i < 3 ? 0x00400040U : (i == 3 ? 0x00210021U : (i == 4 ? 0x00230023U : 0x00270027U));
        r.cycle.observe(r.feedback, now);
        const auto result = r.cycle.tick(r.input, now);
        check(i < 6 ? result.output.command.is_zero() : result.output.command.left_rpm == 60,
              "source snapshot to final RPDO");
    }
    std::atomic<bool> done{false};
    std::thread producer{[&] {
        for (int i = 0; i < 1000; ++i) {
            now += 1ms;
            static_cast<void>(source.consume({std::span{&event, 1}, now, 1}, now));
        }
        done.store(true);
    }};
    std::uint64_t seq = 0;
    do {
        const auto value = source.snapshot();
        check(value.sample.coherent && value.sample.sequence >= seq, "concurrent snapshot sequence");
        check(value.sample.command.left_rpm == value.candidate.left_rpm, "concurrent snapshot whole value");
        seq = value.sample.sequence;
    } while (!done.load());
    producer.join();
}
} // namespace
/** Run pure P10.1 contracts without Linux devices, sleeps or a test framework. */
int main() {
    pipeline();
    invalidation();
    handover_and_boundaries();
    independent_losses();
    sbus_snapshots();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
