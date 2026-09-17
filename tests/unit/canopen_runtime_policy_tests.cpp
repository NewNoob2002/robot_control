#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include "domain/drive/runtime.hpp"

namespace {
using namespace std::chrono_literals;
using namespace robot_control::domain;
using drive::RuntimePolicy;
using drive::RuntimeReason;
using drive::RuntimeRequest;
int failures = 0;
/** Record contract failures in all build types, including NDEBUG. */
void check(bool ok, const char* text) {
    if (!ok) {
        ++failures;
        std::cerr << text << '\n';
    }
}
#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__)
/** Build a test-only binding; no physical deployment profile is implied. */
drive::RuntimeConfig config() {
    return {.left_target = drive::PackedHalf::low,
            .left_feedback = drive::PackedHalf::low,
            .left_sign = 1,
            .right_sign = 1,
            .max_abs_rpm = 100,
            .standstill_tenths_rpm = 0,
            .heartbeat_timeout = 100ms,
            .feedback_timeout = 50ms,
            .decision_timeout = 20ms};
}
/** Build independent raw fixture values at one monotonic time. */
drive::RuntimeFeedback feedback(time::MonotonicTime now) {
    return {.generation = {1, 0},
            .version = 1,
            .heartbeat_at = now,
            .status_at = now,
            .diagnostics_at = now,
            .status_raw = 0x14271427U,
            .velocity_raw = 0,
            .fault_raw = 0,
            .mode_raw = 3,
            .current = true,
            .operational = true};
}
/** Make a new owner decision bound to the current feedback epoch. */
RuntimeRequest request(const RuntimePolicy& policy, time::MonotonicTime now, std::uint64_t seq, std::uint64_t auth = 1,
                       std::int32_t left = 0, std::int32_t right = 0) {
    RuntimeRequest r;
    r.generation = policy.state().feedback.generation;
    r.feedback_epoch = policy.state().epoch;
    r.authorization = auth;
    r.issued_at = now;
    r.decision = {.state = safety::SafetyState::normal,
                  .action =
                      (left || right) ? safety::DriveAction::approved_target : safety::DriveAction::enable_operation,
                  .approved_command = {left, right, false},
                  .authorization_generation = auth,
                  .decision_generation = seq,
                  .motion_approved = left != 0 || right != 0};
    return r;
}
/** Qualify a new authorization with a sent-zero phase and a newer zero sample. */
void arm(RuntimePolicy& p, time::MonotonicTime now) {
    p.observe(feedback(now), now);
    const auto out = p.evaluate(request(p, now, 1), now);
    CHECK(out.accepted && out.command.is_zero());
    auto f = feedback(now + 1ms);
    f.version = 2;
    p.observe(f, now + 1ms);
}
/** Exercise wire vectors, strict boundaries, revocation and independent inputs. */
void test_policy() {
    const time::MonotonicTime t{1s};
    CHECK(!drive::valid_runtime_config({}));
    for (int sign : {0, 2, -2}) {
        auto c = config();
        c.left_sign = sign;
        CHECK(!drive::valid_runtime_config(c));
    }
    auto c = config();
    c.standstill_tenths_rpm = 15;
    CHECK(drive::valid_runtime_config(c));
    c.standstill_tenths_rpm = 20;
    CHECK(drive::valid_runtime_config(c));
    c.standstill_tenths_rpm = 21;
    CHECK(!drive::valid_runtime_config(c));
    c = config();
    c.max_abs_rpm = 1001;
    CHECK(!drive::valid_runtime_config(c));
    c = config();
    c.decision_timeout = 0ms;
    CHECK(!drive::valid_runtime_config(c));
    // A transient configured emergency bit must retire the armed authority even
    // when the next status sample clears before the control-cycle tick.
    c = config();
    c.emergency_status_mask = 0x8000;
    RuntimePolicy emergency{c};
    arm(emergency, t);
    const auto before = emergency.state().epoch;
    auto x1 = feedback(t+2ms);
    x1.version = 3;
    x1.status_raw |= 0x8000;
    emergency.observe(x1, t+2ms);
    CHECK(!emergency.state().healthy && !emergency.state().armed && emergency.state().epoch > before);
    x1.status_raw &= ~0x8000U;
    x1.version = 4;
    emergency.observe(x1, t+3ms);
    CHECK(emergency.state().healthy && !emergency.state().armed);
    CHECK(!emergency.evaluate(request(emergency, t+3ms, 2, 1, 5, 0), t+3ms).accepted);
    // Physical Quick Stop Active cannot be recovered by Shutdown alone.
    RuntimePolicy recovery{config()};
    auto stopped = feedback(t);
    stopped.status_raw = 0x00070007;
    recovery.observe(stopped, t);
    auto recover = request(recovery, t, 1);
    recover.decision.action = safety::DriveAction::shutdown;
    CHECK(recovery.evaluate(recover, t).payload[0] == std::byte{0});
    stopped.status_raw = 0x00400040;
    stopped.version = 2;
    stopped.status_at = t+1ms;
    recovery.observe(stopped, t+1ms);
    recover = request(recovery, t+1ms, 2);
    recover.decision.action = safety::DriveAction::shutdown;
    CHECK(recovery.evaluate(recover, t+1ms).payload[0] == std::byte{6});
    RuntimePolicy invalid{{}};
    CHECK(!invalid.evaluate({}, t).accepted);
    RuntimePolicy p{config()};
    arm(p, t);
    auto out = p.evaluate(request(p, t + 2ms, 2, 1, 5, -6), t + 2ms);
    CHECK(out.accepted
          && out.payload
                 == (std::array<std::byte, 6>{std::byte{15}, std::byte{0}, std::byte{5}, std::byte{0}, std::byte{250},
                                              std::byte{255}}));
    out = p.evaluate(
        request(p, t + 3ms, 3, 1, std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::min()),
        t + 3ms);
    CHECK(out.accepted && out.command.left_rpm == 100 && out.command.right_rpm == -100);
    CHECK(!p.evaluate(request(p, t + 3ms, 3, 1, 5, 0), t + 3ms).accepted);
    CHECK(!p.state().armed);
    CHECK(!p.evaluate(request(p, t + 4ms, 4, 1, 5, 0), t + 4ms).accepted);
    CHECK(!p.evaluate(request(p, t + 5ms, 5, 2, 5, 0), t + 5ms).accepted);
    CHECK(!p.evaluate(request(p, t + 6ms, 6, 2), t + 6ms).accepted); // Nonneutral new generation consumed.
    CHECK(p.evaluate(request(p, t + 7ms, 7, 3), t + 7ms).accepted);
    CHECK(!p.evaluate(request(p, t + 8ms, 8, 3, 5, 0), t + 8ms).accepted); // Old zero feedback cannot authorize motion.

    c = config();
    c.left_target = drive::PackedHalf::high;
    c.left_feedback = drive::PackedHalf::high;
    c.left_sign = -1;
    RuntimePolicy reversed{c};
    arm(reversed, t);
    out = reversed.evaluate(request(reversed, t + 2ms, 2, 1, 5, 6), t + 2ms);
    CHECK(out.payload
          == (std::array<std::byte, 6>{std::byte{15}, std::byte{0}, std::byte{6}, std::byte{0}, std::byte{251},
                                       std::byte{255}}));
    auto f = feedback(t + 3ms);
    f.version = 3;
    f.velocity_raw = 0xFFCD003CU;
    reversed.observe(f, t + 3ms);
    CHECK(reversed.state().left_tenths_rpm == 51 && reversed.state().right_tenths_rpm == 60);

    for (int age : {19, 20, 21}) {
        RuntimePolicy q{config()};
        arm(q, t);
        CHECK(q.evaluate(request(q, t + 1ms, 2), t + 1ms).accepted);
        auto r = request(q, t + 1ms, 3, 1, 5, 0);
        r.issued_at = t + 1ms;
        out = q.evaluate(r, t + 1ms + std::chrono::milliseconds{age});
        CHECK(out.accepted == (age == 19));
        CHECK(age == 19 || out.command.is_zero());
    }
    for (int age : {49, 50, 51}) {
        RuntimePolicy q{config()};
        auto fresh = feedback(t);
        fresh.heartbeat_at = t + std::chrono::milliseconds{age};
        q.observe(fresh, t + std::chrono::milliseconds{age});
        CHECK(q.state().healthy == (age == 49));
    }
    for (int fault_case = 0; fault_case < 9; ++fault_case) {
        RuntimePolicy q{config()};
        arm(q, t);
        CHECK(q.evaluate(request(q, t + 2ms, 2, 1, 5, 0), t + 2ms).accepted);
        auto bad = feedback(t + 3ms);
        bad.version = 3;
        switch (fault_case) {
            case 0:
                bad.status_raw = 0x14271408U;
                break;
            case 1:
                bad.status_raw = 0x14081427U;
                break;
            case 2:
                bad.fault_raw = 0x10000U;
                break;
            case 3:
                bad.mode_raw = 1;
                break;
            case 4:
                bad.current = false;
                break;
            case 5:
                bad.operational = false;
                break;
            case 6:
                ++bad.generation.boot;
                break;
            case 7:
                bad.status_at = t + 4ms;
                break;
            case 8:
                bad.status_raw = 0x14271423U;
                break;
            default:
                CHECK(false);
                break;
        }
        const auto prior_epoch = q.state().epoch;
        q.observe(bad, t + 3ms);
        CHECK(!q.state().armed && q.state().output.command.is_zero());
        CHECK(q.state().epoch > prior_epoch);
        auto good = feedback(t + 4ms);
        good.version = 4;
        good.generation = bad.generation;
        q.observe(good, t + 4ms);
        CHECK(!q.evaluate(request(q, t + 5ms, 3, 1, 5, 0), t + 5ms).accepted);
    }
    for (int axis = 0; axis < 3; ++axis) {
        for (int offset : {-1, 0, 1}) {
            RuntimePolicy q{config()};
            auto sample = feedback(t);
            const auto limit = axis == 0 ? 100ms : 50ms;
            const auto now = t + limit + std::chrono::milliseconds{offset};
            sample.heartbeat_at = now;
            sample.status_at = now;
            sample.diagnostics_at = now;
            if (axis == 0) {
                sample.heartbeat_at = t;
            }
            if (axis == 1) {
                sample.status_at = t;
            }
            if (axis == 2) {
                sample.diagnostics_at = t;
            }
            q.observe(sample, now);
            CHECK(q.state().healthy == (offset < 0));
        }
    }
    RuntimePolicy decelerating{config()};
    arm(decelerating, t);
    CHECK(decelerating.evaluate(request(decelerating, t + 2ms, 2, 1, 5, 0), t + 2ms).accepted);
    auto moving = feedback(t + 3ms);
    moving.version = 3;
    moving.velocity_raw = 50;
    decelerating.observe(moving, t + 3ms);
    out = decelerating.evaluate(request(decelerating, t + 4ms, 3), t + 4ms);
    CHECK(out.accepted && out.payload[0] == std::byte{15} && decelerating.state().armed);
    moving.version = 4;
    moving.status_at = t + 5ms;
    moving.status_raw = 0x14231427U;
    decelerating.observe(moving, t + 5ms);
    CHECK(!decelerating.state().armed);
    RuntimePolicy waiting{config()};
    waiting.observe(feedback(t), t);
    CHECK(waiting.evaluate(request(waiting, t, 1), t).accepted);
    auto wait_request = request(waiting, t + 1ms, 2);
    wait_request.decision.action = safety::DriveAction::shutdown;
    out = waiting.evaluate(wait_request, t + 1ms);
    CHECK(out.accepted && out.command.is_zero() && out.payload[0] == std::byte{6} && waiting.state().armed);
    CHECK(!waiting.evaluate(request(waiting, t + 2ms, 3, 1, 5, 0), t + 2ms).accepted);
    RuntimePolicy no_zero{config()};
    no_zero.observe(feedback(t), t);
    CHECK(no_zero.evaluate(request(no_zero, t, 1), t).accepted);
    moving = feedback(t + 1ms);
    moving.version = 2;
    moving.velocity_raw = 1;
    no_zero.observe(moving, t + 1ms);
    CHECK(!no_zero.evaluate(request(no_zero, t + 2ms, 2, 1, 5, 0), t + 2ms).accepted);
    for (auto action : {safety::DriveAction::quick_stop, safety::DriveAction::disable_voltage}) {
        RuntimePolicy q{config()};
        arm(q, t);
        auto r = request(q, t + 2ms, 2);
        r.decision.action = action;
        r.decision.state = safety::SafetyState::emergency_stop;
        out = q.evaluate(r, t + 2ms);
        CHECK(out.accepted && out.command.is_zero() && !q.state().armed);
        CHECK(out.payload[0] == (action == safety::DriveAction::quick_stop ? std::byte{2} : std::byte{0}));
    }
    RuntimePolicy transitions{config()};
    auto status_sample = feedback(t);
    status_sample.status_raw = 0x14401440U;
    transitions.observe(status_sample, t);
    CHECK(transitions.evaluate(request(transitions, t, 1), t).accepted);
    for (int step = 0; step < 3; ++step) {
        status_sample.version = static_cast<std::uint64_t>(step) + 2U;
        status_sample.status_at = t + std::chrono::milliseconds{step + 1};
        status_sample.status_raw = step == 0 ? 0x14211421U : (step == 1 ? 0x14231423U : 0x14271427U);
        transitions.observe(status_sample, status_sample.status_at);
        auto intent = request(transitions, status_sample.status_at, static_cast<std::uint64_t>(step) + 2U);
        intent.decision.action = step == 0 ? safety::DriveAction::switch_on : safety::DriveAction::enable_operation;
        out = transitions.evaluate(intent, status_sample.status_at);
        CHECK(out.accepted && out.command.is_zero());
        CHECK(out.payload[0] == (step == 0 ? std::byte{7} : std::byte{15}));
    }
    status_sample.version = 5;
    status_sample.status_at = t + 4ms;
    status_sample.status_raw = 0x14231427U;
    transitions.observe(status_sample, t + 4ms);
    CHECK(!transitions.state().armed);
    RuntimePolicy clock{config()};
    arm(clock, t);
    clock.observe(feedback(t), t);
    CHECK(clock.state().stopped);
    RuntimePolicy future{config()};
    arm(future, t);
    CHECK(!future.evaluate(request(future, t + 3ms, 2), t + 2ms).accepted);
    RuntimePolicy expiry{config()};
    arm(expiry, t);
    expiry.observe(feedback(t + 20ms), t + 20ms);
    CHECK(!expiry.state().armed);
    RuntimePolicy exhausted{config()};
    arm(exhausted, t);
    CHECK(
        !exhausted.evaluate(request(exhausted, t + 2ms, std::numeric_limits<std::uint64_t>::max()), t + 2ms).accepted);
    exhausted.stop();
    CHECK(exhausted.state().stopped);
    CHECK(!exhausted.evaluate(request(exhausted, t + 3ms, 3, 2), t + 3ms).accepted);
}
} // namespace
/** Run pure runtime contract tests without Linux devices. */
int main() {
    test_policy();
    return failures == 0 ? 0 : 1;
}
