#include "application/control/external_authority.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
using namespace std::chrono_literals;
using namespace robot_control;
using namespace domain;

using application::control::AuthorityStatus;
using application::control::CycleConfig;
using application::control::CycleInput;
using application::control::CycleResult;
using application::control::ExternalAuthority;
using application::control::ExternalChallenge;
using application::control::ExternalKind;
using application::control::ExternalPhase;
using application::control::ExternalReply;
using application::control::ExternalVerdict;
using application::control::Id128;

int failures = 0;
/** Record an independent contract assertion, including in Release builds. */
void check(bool ok, const char* label) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL " << label << '\n';
    }
}
/** Unwrap an expected challenge, recording a failure instead of dereferencing an empty value. */
ExternalChallenge take(const std::optional<ExternalChallenge>& issued) {
    check(issued.has_value(), "expected challenge was refused");
    return issued.value_or(ExternalChallenge{});
}
/** Build an explicit virtual binding, never a physical deployment default. */
CycleConfig config() {
    CycleConfig c;
    c.runtime = {drive::PackedHalf::low, drive::PackedHalf::low, 1, 1, 100, 0, 100ms, 100ms, 30ms};
    c.arbiter.handover_zero_dwell = 20ms;
    c.arbiter.max_abs_rpm = 100;
    return c;
}
/** Construct one immutable enabled SBUS sample. */
command::CommandSample sbus_sample(time::MonotonicTime now, std::uint64_t seq, std::uint64_t auth, int rpm) {
    return {command::Source::sbus, {rpm, rpm, false}, now, 1, auth, seq, true, true, true, false, false};
}

/**
 * Offline owner composition: real authority, real ControlCycle and the sole RuntimePolicy guard.
 * A minimal CiA402 emulation answers each accepted controlword on the next feedback event.
 */
struct Rig {
    CycleConfig cfg{config()};
    drive::RuntimePolicy runtime{cfg.runtime};
    application::control::ControlCycle cycle{cfg};
    ExternalAuthority authority{cfg, Id128{7, 11}};
    time::MonotonicTime now{};
    CycleInput input{.coherent = true, .emergency_stop_known = true};
    drive::RuntimeFeedback feedback{
        .generation = {1, 0}, .status_raw = 0x00400040, .mode_raw = 3, .current = true, .operational = true};
    std::uint64_t sbus_sequence{0};
    std::uint64_t sbus_authorization{1};
    int sbus_rpm{0};
    std::uint64_t reply_sequence{0};
    std::uint64_t nonce{0};
    bool feedback_live{true};
    ExternalChallenge last_challenge{};
    time::MonotonicTime last_challenge_at{};

    /** Issue a fresh challenge at the current owner time. */
    std::optional<ExternalChallenge> challenge() {
        auto issued = authority.challenge(Id128{0, ++nonce}, now);
        if (issued) {
            last_challenge = *issued;
            last_challenge_at = now;
        }
        return issued;
    }
    /** Producer round trip: challenge, re-sample, reply after the given latency. */
    ExternalVerdict reply(ExternalKind kind, int rpm = 0, bool stop = false, time::Duration latency = 1ms) {
        const auto issued = challenge();
        if (!issued)
            return ExternalVerdict::no_challenge;
        // Owner time advances to the receipt; issuing a later challenge earlier would be a clock regression.
        now += latency;
        return authority.submit({take(issued), kind, ++reply_sequence, rpm, rpm, stop}, now);
    }
    /** Advance one nominal cycle with fresh SBUS and drive feedback, then run the owner once. */
    CycleResult tick(time::Duration dt = 10ms) {
        now += dt;
        ++sbus_sequence;
        input.sbus = sbus_sample(now, sbus_sequence, sbus_authorization, sbus_rpm);
        if (feedback_live) {
            feedback.version = sbus_sequence;
            feedback.heartbeat_at = feedback.status_at = feedback.diagnostics_at = now;
            runtime.observe(feedback, now);
        }
        runtime.observe(runtime.state().feedback, now);
        input.external = authority.update(input.sbus, runtime.state(), cycle, input.shutdown_requested, now);
        auto result = cycle.prepare(input, runtime.state(), now);
        static_cast<void>(runtime.evaluate(result.request, now));
        cycle.complete(result, runtime.state(), now);
        if (result.output.accepted) {
            const auto word = std::to_integer<unsigned>(result.output.payload[0]);
            if (word == 0U)
                feedback.status_raw = 0x00400040;
            if (word == 6U)
                feedback.status_raw = 0x00210021;
            if (word == 7U)
                feedback.status_raw = 0x00230023;
            if (word == 15U)
                feedback.status_raw = 0x00270027;
        }
        return result;
    }
    /** Producer sends one reply per cycle; returns the cycle result. */
    CycleResult tick_with(ExternalKind kind, int rpm = 0) {
        static_cast<void>(reply(kind, rpm));
        return tick();
    }
    /** Hold neutral replies across the zero dwell so an authorize edge can qualify. */
    void qualify() {
        for (int i = 0; i < 4; ++i)
            static_cast<void>(tick_with(ExternalKind::neutral));
    }
    /** Complete qualification, authorize and drive the owner until external is active and enabled. */
    bool authorize_and_enable() {
        qualify();
        if (reply(ExternalKind::authorize) != ExternalVerdict::validated)
            return false;
        for (int i = 0; i < 40; ++i) {
            const auto result = tick_with(ExternalKind::command, 0);
            if (authority.phase() != ExternalPhase::authorized)
                return false;
            if (cycle.authority_status(command::Source::external, last_challenge.connection_generation,
                                       authority.authorization_generation())
                    == AuthorityStatus::active
                && feedback.status_raw == 0x00270027U && result.output.accepted)
                return true;
        }
        return false;
    }
};

/** Neutral observation alone never produces an external sample or enables an external owner. */
void observation_never_authorizes() {
    Rig r;
    check(r.authority.connect() == 1, "first connection generation");
    for (int i = 0; i < 40; ++i) {
        const auto result = r.tick_with(ExternalKind::neutral);
        check(!r.input.external.valid, "no external sample before authorization");
        check(result.selected.source != command::Source::external, "observation never selected");
    }
    check(r.authority.phase() == ExternalPhase::qualifying, "neutral hold qualifies only");
    check(r.authority.authorization_generation() == 0, "no generation without authorize edge");
}

/** Normal handover: neutral hold, explicit authorize, owner zero hold, CiA402 enable, then motion. */
void normal_handover() {
    Rig r;
    static_cast<void>(r.authority.connect());
    check(r.authorize_and_enable(), "authorize then enable external");
    check(r.authority.authorization_generation() == 1, "first explicit generation");
    static_cast<void>(r.reply(ExternalKind::command, 40));
    const auto t0 = r.last_challenge_at;
    const auto result = r.tick();
    check(result.selected.source == command::Source::external && result.output.command.left_rpm == 40,
          "authorized external motion");
    check(r.input.external.captured_at == t0, "sample uses board challenge time t0");
}

/** Authorization edge rules: neutral-only, after the dwell, never cached, never repeated by holding. */
void authorization_edges() {
    Rig r;
    static_cast<void>(r.authority.connect());
    static_cast<void>(r.tick_with(ExternalKind::neutral));
    check(r.reply(ExternalKind::authorize) == ExternalVerdict::not_qualified, "authorize before dwell");
    check(r.authority.phase() == ExternalPhase::revoked, "premature authorize clears qualification");
    r.qualify();
    check(r.reply(ExternalKind::authorize, 10) == ExternalVerdict::not_neutral, "non-neutral authorize");
    check(r.authority.phase() == ExternalPhase::revoked, "non-neutral authorize clears qualification");
    r.qualify();
    check(r.reply(ExternalKind::authorize, 0, true) == ExternalVerdict::not_neutral, "stop is not authorize");
    r.qualify();
    check(r.reply(ExternalKind::authorize) == ExternalVerdict::validated, "authorize after full hold");
    check(r.reply(ExternalKind::authorize) == ExternalVerdict::wrong_phase, "held authorize not a new edge");
    check(r.authority.authorization_generation() == 1, "held authorize keeps generation");
    check(r.reply(ExternalKind::neutral) == ExternalVerdict::wrong_phase, "neutral is not a command");

    Rig early;
    static_cast<void>(early.authority.connect());
    for (int i = 0; i < 4; ++i) {
        check(early.reply(ExternalKind::authorize) != ExternalVerdict::validated, "authorize spam never qualifies");
        static_cast<void>(early.tick());
    }
    check(early.authority.authorization_generation() == 0, "authorize spam issues no generation");

    Rig dwell;
    static_cast<void>(dwell.authority.connect());
    static_cast<void>(dwell.tick());
    check(dwell.reply(ExternalKind::neutral) == ExternalVerdict::validated, "dwell neutral accepted");
    check(dwell.authority.phase() == ExternalPhase::qualifying, "dwell qualification started");
    check(dwell.reply(ExternalKind::authorize, 0, false, 18ms) == ExternalVerdict::not_qualified,
          "authorize one tick before the dwell boundary");
}

/** Qualification is cleared by SBUS deflection, a neutral gap, drive motion or disconnection. */
void qualification_resets() {
    {
        Rig r;
        static_cast<void>(r.authority.connect());
        r.qualify();
        r.sbus_rpm = 20;
        static_cast<void>(r.tick_with(ExternalKind::neutral));
        r.sbus_rpm = 0;
        check(r.reply(ExternalKind::authorize) == ExternalVerdict::not_qualified, "SBUS deflection clears hold");
    }
    {
        Rig r;
        static_cast<void>(r.authority.connect());
        r.qualify();
        for (int i = 0; i < 10; ++i)
            static_cast<void>(r.tick());
        check(r.authority.phase() == ExternalPhase::revoked, "neutral gap expires qualification");
        check(r.reply(ExternalKind::authorize) == ExternalVerdict::not_qualified, "gap requires new hold");
    }
    {
        Rig r;
        static_cast<void>(r.authority.connect());
        r.qualify();
        r.feedback.velocity_raw = 50;
        static_cast<void>(r.tick_with(ExternalKind::neutral));
        check(r.reply(ExternalKind::authorize) == ExternalVerdict::not_qualified, "moving drive clears hold");
    }
    {
        Rig r;
        static_cast<void>(r.authority.connect());
        r.qualify();
        r.authority.disconnect();
        check(r.authority.phase() == ExternalPhase::revoked, "disconnect clears hold");
        check(r.reply(ExternalKind::authorize) == ExternalVerdict::no_challenge, "disconnected has no challenge");
    }
}

/** Reply validation: challenge lifetime, consumption, sequence floor, binding, range and clock. */
void reply_validation() {
    Rig r;
    check(!r.challenge(), "no challenge before connection");
    static_cast<void>(r.authority.connect());
    check(r.authority.submit({}, r.now) == ExternalVerdict::no_challenge, "reply without challenge");
    check(!r.challenge(), "no challenge before a drive generation is observed");
    static_cast<void>(r.tick());

    auto issued = r.challenge();
    check(issued.has_value(), "challenge issued");
    check(!r.authority.challenge(Id128{0, 99}, r.now), "one outstanding challenge");
    check(r.authority.submit({take(issued), ExternalKind::neutral, 1, 0, 0, false}, r.now + 100ms)
              == ExternalVerdict::expired,
          "age equal to timeout rejected");
    check(r.authority.submit({take(issued), ExternalKind::neutral, 2, 0, 0, false}, r.now + 100ms)
              == ExternalVerdict::no_challenge,
          "consumed challenge cannot be resubmitted");

    r.now += 100ms;
    issued = r.challenge();
    check(r.authority.submit({take(issued), ExternalKind::neutral, 3, 0, 0, false}, r.now + 100ms - 1ns)
              == ExternalVerdict::validated,
          "age one nanosecond below timeout accepted");
    r.now += 100ms;
    issued = r.challenge();
    check(r.authority.submit({take(issued), ExternalKind::neutral, 3, 0, 0, false}, r.now)
              == ExternalVerdict::sequence_replay,
          "repeated sequence rejected");
    issued = r.challenge();
    check(r.authority.submit({take(issued), ExternalKind::neutral, 2, 0, 0, false}, r.now)
              == ExternalVerdict::sequence_replay,
          "older sequence rejected");

    const auto stale = take(r.challenge());
    static_cast<void>(r.authority.submit({stale, ExternalKind::neutral, 10, 0, 0, false}, r.now));
    static_cast<void>(r.challenge());
    check(r.authority.submit({stale, ExternalKind::neutral, 11, 0, 0, false}, r.now)
              == ExternalVerdict::challenge_mismatch,
          "cached reply under a new challenge");

    const auto tamper = [&](auto mutate) {
        auto bound = take(r.challenge());
        mutate(bound);
        return r.authority.submit({bound, ExternalKind::neutral, ++r.reply_sequence + 100, 0, 0, false}, r.now);
    };
    check(tamper([](ExternalChallenge& c) {
              ++c.app_instance_id.low;
          }) == ExternalVerdict::binding_mismatch,
          "tampered instance");
    check(tamper([](ExternalChallenge& c) {
              ++c.connection_generation;
          }) == ExternalVerdict::binding_mismatch,
          "tampered connection");
    check(tamper([](ExternalChallenge& c) {
              ++c.authorization_generation;
          }) == ExternalVerdict::binding_mismatch,
          "rewrapped authorization generation");
    check(tamper([](ExternalChallenge& c) {
              ++c.drive_generation.boot;
          }) == ExternalVerdict::binding_mismatch,
          "tampered drive generation");

    r.reply_sequence = 1000;
    check(r.reply(ExternalKind::neutral, 0) == ExternalVerdict::validated, "valid after rejections");
    issued = r.challenge();
    check(r.authority.submit({take(issued), ExternalKind::neutral, ++r.reply_sequence, 101, 0, false}, r.now)
              == ExternalVerdict::rpm_range,
          "rpm above limit rejected, not clamped");

    issued = r.challenge();
    check(r.authority.submit(
              {take(issued), ExternalKind::neutral, std::numeric_limits<std::uint64_t>::max(), 0, 0, false}, r.now)
              == ExternalVerdict::validated,
          "maximum sequence accepted once");
    check(r.reply(ExternalKind::neutral) == ExternalVerdict::sequence_replay, "sequence never wraps");

    Rig nonce;
    static_cast<void>(nonce.authority.connect());
    static_cast<void>(nonce.tick());
    check(!nonce.authority.challenge(Id128{}, nonce.now), "empty nonce refused");
    check(nonce.authority.challenge(Id128{1, 2}, nonce.now).has_value(), "nonce accepted");
    nonce.now += 100ms;
    check(!nonce.authority.challenge(Id128{1, 2}, nonce.now), "repeated nonce refused");

    Rig clock;
    static_cast<void>(clock.authority.connect());
    static_cast<void>(clock.tick());
    clock.now += 10ms;
    issued = clock.challenge();
    check(clock.authority.submit({take(issued), ExternalKind::neutral, 1, 0, 0, false}, clock.now - 1ms)
              == ExternalVerdict::inhibited,
          "clock regression inhibits");
    check(!clock.challenge(), "clock regression is terminal");
    check(clock.authority.connect() == 0, "terminal authority refuses reconnection");

    ExternalAuthority invalid{config(), Id128{}};
    check(invalid.connect() == 0, "empty instance identity permanently inhibits");
}

/**
 * Pin SBUS interlock freshness independently of external candidate age.
 * @return No value; records failures through the owner-thread test counter.
 * Thread safety: Test-owner only; each fixture owns its clocks and state.
 */
void sbus_age_boundaries() {
    const std::array ages{100ms + 0ns, 100ms - 1ns, -1ns};
    for (const auto age : ages) {
        Rig r;
        static_cast<void>(r.authority.connect());
        check(r.authorize_and_enable(), "SBUS age boundary setup");
        auto sbus = r.input.sbus;
        sbus.captured_at = r.now - age;
        const auto result = r.authority.update(sbus, r.runtime.state(), r.cycle, false, r.now);
        check(result.valid == (age == 100ms - 1ns), "SBUS exclusive timeout and future boundary");
    }
    Rig r;
    static_cast<void>(r.authority.connect());
    check(r.authorize_and_enable(), "negative SBUS stamp setup");
    auto sbus = r.input.sbus;
    sbus.captured_at = time::MonotonicTime{} - 1ns;
    check(!r.authority.update(sbus, r.runtime.state(), r.cycle, false, r.now).valid,
          "SBUS stamp before epoch rejected");
}

/** Forwarding never refreshes age; a silent producer expires and revokes. */
void silent_producer_expires() {
    Rig r;
    static_cast<void>(r.authority.connect());
    check(r.authorize_and_enable(), "silent setup");
    static_cast<void>(r.reply(ExternalKind::command, 40));
    const auto t0 = r.last_challenge_at;
    bool moved = false;
    for (int i = 0; i < 12; ++i) {
        const auto result = r.tick();
        if (r.input.external.valid)
            check(r.input.external.captured_at == t0, "forwarded sample keeps t0");
        moved = moved || result.output.command.left_rpm == 40;
        if (r.now - t0 >= 100ms) {
            check(!r.input.external.valid, "expired candidate not forwarded");
            check(result.output.command.is_zero(), "expired candidate outputs zero");
        }
    }
    check(moved, "fresh candidate moved before expiry");
    check(r.authority.phase() == ExternalPhase::revoked, "silent producer revoked");
    check(r.reply(ExternalKind::command, 40) == ExternalVerdict::wrong_phase, "late command after revocation");
}

/** Restarts and generation changes invalidate old candidates; recovery stays revoked. */
void restarts_and_generations() {
    {
        Rig r;
        static_cast<void>(r.authority.connect());
        check(r.authorize_and_enable(), "reconnect setup");
        const auto old = take(r.challenge());
        check(r.authority.connect() == 2, "reconnection increments generation");
        check(r.authority.phase() == ExternalPhase::revoked, "reconnection revokes");
        check(r.authority.submit({old, ExternalKind::command, 1000, 40, 40, false}, r.now)
                  == ExternalVerdict::no_challenge,
              "old connection challenge dropped");
        const auto result = r.tick();
        check(!r.input.external.valid && result.output.command.is_zero(), "reconnection outputs zero");
        check(r.reply(ExternalKind::command, 40) == ExternalVerdict::wrong_phase, "healthy reconnect stays revoked");
    }
    {
        Rig r;
        static_cast<void>(r.authority.connect());
        static_cast<void>(r.tick());
        const auto old = take(r.challenge());
        ExternalAuthority restarted{r.cfg, Id128{8, 12}};
        static_cast<void>(restarted.connect());
        static_cast<void>(restarted.update(r.input.sbus, r.runtime.state(), r.cycle, false, r.now));
        const auto fresh = restarted.challenge(old.challenge_id, r.now);
        check(fresh.has_value() && fresh->challenge_id == old.challenge_id, "restart reuses colliding nonce");
        check(restarted.submit({old, ExternalKind::neutral, 1, 0, 0, false}, r.now)
                  == ExternalVerdict::binding_mismatch,
              "app restart rejects old instance binding");
    }
    {
        Rig r;
        static_cast<void>(r.authority.connect());
        check(r.authorize_and_enable(), "drive boot setup");
        ++r.feedback.generation.boot;
        static_cast<void>(r.reply(ExternalKind::command, 40));
        const auto result = r.tick();
        check(result.output.command.is_zero(), "drive boot outputs zero");
        check(r.authority.phase() == ExternalPhase::revoked, "drive boot revokes authority");
    }
    {
        Rig r;
        static_cast<void>(r.authority.connect());
        check(r.authorize_and_enable(), "takeover setup");
        static_cast<void>(r.tick_with(ExternalKind::command, 40));
        r.sbus_rpm = 30;
        const auto takeover = r.tick_with(ExternalKind::command, 40);
        check(takeover.selected.source == command::Source::sbus, "SBUS takeover selected");
        check(takeover.output.command.left_rpm != 40, "external target dropped on takeover");
        check(r.authority.phase() == ExternalPhase::revoked, "takeover revokes external");
        r.sbus_rpm = 0;
        for (int i = 0; i < 5; ++i)
            static_cast<void>(r.tick_with(ExternalKind::command, 40));
        check(!r.input.external.valid, "external stays revoked after SBUS returns");
        check(r.authorize_and_enable(), "return requires new hold and authorization");
        check(r.authority.authorization_generation() == 2, "return uses a new generation");
    }
}

/** Inhibition takes priority over a fresh valid candidate in the same cycle. */
void inhibition_priority() {
    {
        Rig r;
        static_cast<void>(r.authority.connect());
        check(r.authorize_and_enable(), "estop setup");
        r.input.emergency_stop_active = true;
        check(r.tick_with(ExternalKind::command, 40).output.command.is_zero(), "emergency stop wins");
    }
    {
        Rig r;
        static_cast<void>(r.authority.connect());
        check(r.authorize_and_enable(), "feedback loss setup");
        r.feedback_live = false;
        bool zero = false;
        for (int i = 0; i < 12; ++i)
            zero = r.tick_with(ExternalKind::command, 40).output.command.is_zero();
        check(zero, "feedback loss outputs zero");
        check(r.authority.phase() == ExternalPhase::revoked, "feedback loss revokes authority");
    }
    {
        Rig r;
        static_cast<void>(r.authority.connect());
        check(r.authorize_and_enable(), "shutdown setup");
        static_cast<void>(r.reply(ExternalKind::command, 40));
        r.input.shutdown_requested = true;
        const auto result = r.tick();
        check(result.output.command.is_zero(), "shutdown outputs zero");
        check(result.request.decision.state == safety::SafetyState::shutdown, "shutdown decision");
        check(r.authority.connect() == 0, "no reauthorization after shutdown");
    }
    {
        Rig r;
        static_cast<void>(r.authority.connect());
        check(r.authorize_and_enable(), "stop request setup");
        check(r.reply(ExternalKind::command, 0, true) == ExternalVerdict::validated, "stop request accepted");
        check(r.authority.phase() == ExternalPhase::revoked, "stop request relinquishes authority");
        check(r.tick().output.command.is_zero(), "stop request outputs zero");
    }
}

/** A generation the owner already consumed cannot strand the authority in a dead grant. */
void consumed_generation_revokes() {
    Rig r;
    static_cast<void>(r.authority.connect());
    r.qualify();
    check(r.reply(ExternalKind::authorize) == ExternalVerdict::validated, "consumed setup");
    for (int i = 0; i < 3; ++i) {
        const auto result = r.tick_with(ExternalKind::command, 40);
        check(result.output.command.is_zero(), "nonzero before owner hold outputs zero");
    }
    check(r.authority.phase() == ExternalPhase::revoked, "consumed generation revokes authority");
}
} // namespace

/** Execute the motion v2 external authority contract without an external framework. */
int main() {
    observation_never_authorizes();
    normal_handover();
    authorization_edges();
    qualification_resets();
    reply_validation();
    sbus_age_boundaries();
    silent_producer_expires();
    restarts_and_generations();
    inhibition_priority();
    consumed_generation_revokes();
    if (failures != 0) {
        std::cerr << failures << " external authority contract failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "external authority contract passed\n";
    return EXIT_SUCCESS;
}
