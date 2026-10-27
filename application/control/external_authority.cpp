#include "application/control/external_authority.hpp"

#include <limits>

namespace robot_control::application::control {

ExternalAuthority::ExternalAuthority(const CycleConfig& config, Id128 app_instance_id) noexcept
    : config_{config}, instance_{app_instance_id} {
    stopped_ = instance_.empty() || !valid_cycle_config(config_);
}

bool ExternalAuthority::within_timeout(time::MonotonicTime start, time::MonotonicTime now) const noexcept {
    return time::age_below(start, now, config_.arbiter.external_timeout);
}

void ExternalAuthority::revoke() noexcept {
    phase_ = ExternalPhase::revoked;
    candidate_ = {};
}

std::uint64_t ExternalAuthority::connect() noexcept {
    revoke();
    challenge_live_ = false;
    connected_ = false;
    if (stopped_ || connection_ == std::numeric_limits<std::uint64_t>::max()) {
        stopped_ = true;
        return 0;
    }
    ++connection_;
    sequence_floor_ = 0;
    connected_ = true;
    return connection_;
}

void ExternalAuthority::disconnect() noexcept {
    // connection_ is retained so the next connect() still exceeds every issued generation.
    revoke();
    challenge_live_ = false;
    connected_ = false;
}

std::optional<ExternalChallenge> ExternalAuthority::challenge(Id128 nonce, time::MonotonicTime now) noexcept {
    if (have_now_ && now < last_now_)
        stopped_ = true;
    if (stopped_ || now < time::MonotonicTime{}) {
        stopped_ = true;
        revoke();
        return std::nullopt;
    }
    last_now_ = now;
    have_now_ = true;
    if (!connected_ || !have_drive_generation_ || nonce.empty() || nonce == last_nonce_
        || (challenge_live_ && within_timeout(outstanding_at_, now)))
        return std::nullopt;
    outstanding_ = {.app_instance_id = instance_,
                    .challenge_id = nonce,
                    .connection_generation = connection_,
                    .authorization_generation = authorization_,
                    .drive_generation = drive_generation_};
    outstanding_at_ = now;
    last_nonce_ = nonce;
    challenge_live_ = true;
    return outstanding_;
}

ExternalVerdict ExternalAuthority::submit(const ExternalReply& reply, time::MonotonicTime now) noexcept {
    if (have_now_ && now < last_now_)
        stopped_ = true;
    if (stopped_ || now < time::MonotonicTime{}) {
        stopped_ = true;
        revoke();
        return ExternalVerdict::inhibited;
    }
    last_now_ = now;
    have_now_ = true;
    if (!connected_ || !challenge_live_)
        return ExternalVerdict::no_challenge;
    // Entering validation consumes the challenge; a rejected reply cannot be patched and resent.
    challenge_live_ = false;
    const auto expected = outstanding_;
    const auto issued_at = outstanding_at_;
    if (reply.sequence <= sequence_floor_)
        return ExternalVerdict::sequence_replay;
    sequence_floor_ = reply.sequence;
    if (reply.challenge.challenge_id != expected.challenge_id)
        return ExternalVerdict::challenge_mismatch;
    if (!(reply.challenge == expected))
        return ExternalVerdict::binding_mismatch;
    // The reply was sampled after t0, so now - t0 bounds its age without trusting producer clocks.
    if (!within_timeout(issued_at, now))
        return ExternalVerdict::expired;
    const auto limit = config_.arbiter.max_abs_rpm;
    if (reply.left_rpm < -limit || reply.left_rpm > limit || reply.right_rpm < -limit || reply.right_rpm > limit)
        return ExternalVerdict::rpm_range;
    const bool neutral = !reply.stop_requested && reply.left_rpm == 0 && reply.right_rpm == 0;

    switch (reply.kind) {
        case ExternalKind::neutral:
            if (phase_ == ExternalPhase::authorized)
                return ExternalVerdict::wrong_phase;
            if (!neutral) {
                revoke();
                return ExternalVerdict::not_neutral;
            }
            if (conditions_ok_) {
                if (phase_ == ExternalPhase::revoked) {
                    phase_ = ExternalPhase::qualifying;
                    // The hold starts at receipt, the later bound of the unknown sampling instant.
                    neutral_since_ = now;
                }
                last_neutral_at_ = issued_at;
            }
            return ExternalVerdict::validated;
        case ExternalKind::authorize: {
            if (phase_ == ExternalPhase::authorized)
                return ExternalVerdict::wrong_phase;
            const bool qualified = phase_ == ExternalPhase::qualifying && conditions_ok_
                                   && within_timeout(last_neutral_at_, now)
                                   && now - neutral_since_ >= config_.arbiter.handover_zero_dwell;
            if (!neutral) {
                revoke();
                return ExternalVerdict::not_neutral;
            }
            if (!qualified) {
                // A premature press must be released and requalified; it is never cached.
                revoke();
                return ExternalVerdict::not_qualified;
            }
            if (authorization_ == std::numeric_limits<std::uint64_t>::max()) {
                stopped_ = true;
                revoke();
                return ExternalVerdict::inhibited;
            }
            ++authorization_;
            phase_ = ExternalPhase::authorized;
            candidate_ = {.source = command::Source::external,
                          .command = {.left_rpm = 0, .right_rpm = 0, .stop_requested = false},
                          .captured_at = issued_at,
                          .session_generation = connection_,
                          .authorization_generation = authorization_,
                          .sequence = reply.sequence,
                          .valid = true,
                          .coherent = true,
                          .enabled = true};
            return ExternalVerdict::validated;
        }
        case ExternalKind::command:
            if (phase_ != ExternalPhase::authorized)
                return ExternalVerdict::wrong_phase;
            if (reply.stop_requested) {
                revoke();
                return ExternalVerdict::validated;
            }
            candidate_.command = {.left_rpm = reply.left_rpm, .right_rpm = reply.right_rpm, .stop_requested = false};
            candidate_.captured_at = issued_at;
            candidate_.sequence = reply.sequence;
            return ExternalVerdict::validated;
    }
    return ExternalVerdict::wrong_phase;
}

command::CommandSample ExternalAuthority::update(const command::CommandSample& sbus, const drive::RuntimeState& state,
                                                 const ControlCycle& cycle, bool shutdown_requested,
                                                 time::MonotonicTime now) noexcept {
    if ((have_now_ && now < last_now_) || now < time::MonotonicTime{} || shutdown_requested)
        stopped_ = true;
    if (stopped_) {
        revoke();
        challenge_live_ = false;
        return {};
    }
    last_now_ = now;
    have_now_ = true;
    if (!have_drive_generation_ || state.feedback.generation != drive_generation_) {
        // A new transport or remote boot invalidates every binding issued for the old drive.
        if (have_drive_generation_)
            revoke();
        challenge_live_ = false;
        drive_generation_ = state.feedback.generation;
        have_drive_generation_ = true;
    }
    const bool sbus_fresh = command::structurally_valid(sbus, command::Source::sbus)
                            && time::age_below(sbus.captured_at, now, config_.arbiter.sbus_timeout);
    const bool sbus_neutral = sbus_fresh && sbus.command.is_zero();
    const auto band = config_.runtime.standstill_tenths_rpm;
    const bool stationary = state.left_tenths_rpm >= -band && state.left_tenths_rpm <= band
                            && state.right_tenths_rpm >= -band && state.right_tenths_rpm <= band;
    const bool drive_ok = state.healthy && !state.stopped;
    conditions_ok_ = connected_ && sbus_neutral && drive_ok && stationary;

    if (phase_ == ExternalPhase::qualifying && (!conditions_ok_ || !within_timeout(last_neutral_at_, now)))
        revoke();
    if (phase_ == ExternalPhase::authorized) {
        const auto owner = cycle.authority_status(command::Source::external, connection_, authorization_);
        // SBUS deflection is manual takeover; the old external generation must never return.
        if (!connected_ || !sbus_neutral || !drive_ok || !within_timeout(candidate_.captured_at, now)
            || owner == AuthorityStatus::consumed)
            revoke();
    }
    return phase_ == ExternalPhase::authorized ? candidate_ : command::CommandSample{};
}

ExternalPhase ExternalAuthority::phase() const noexcept {
    return phase_;
}

std::uint64_t ExternalAuthority::authorization_generation() const noexcept {
    return authorization_;
}

} // namespace robot_control::application::control
