#include "domain/drive/runtime.hpp"

#include <algorithm>
#include <limits>

namespace robot_control::domain::drive {
namespace {
using safety::DriveAction;
using zlac8015d::TransitionControlword;
/** Reject future/negative timestamps and the exact expiry boundary. */
bool fresh(time::MonotonicTime stamp, time::MonotonicTime now, time::Duration timeout) noexcept {
    return stamp.time_since_epoch() >= time::Duration::zero() && stamp <= now && now - stamp < timeout;
}
/** Convert a U32 diagnostic word into owned little-endian bytes. */
std::array<std::byte, 4> bytes(std::uint32_t value) noexcept {
    return {std::byte(value & 255U), std::byte((value >> 8U) & 255U), std::byte((value >> 16U) & 255U),
            std::byte((value >> 24U) & 255U)};
}
/** Require both speeds within the configured explicit standstill tolerance. */
bool stationary(const RuntimeState& s, std::int32_t tolerance) noexcept {
    return s.left_tenths_rpm >= -tolerance && s.left_tenths_rpm <= tolerance && s.right_tenths_rpm >= -tolerance
           && s.right_tenths_rpm <= tolerance;
}
/** Detect either neutral status half reporting a drive fault or unknown state. */
bool bad_status(const DualAxisStatus& status) noexcept {
    const auto bad = [](Cia402State state) {
        return state == Cia402State::unknown || state == Cia402State::fault
               || state == Cia402State::fault_reaction_active || state == Cia402State::not_ready_to_switch_on;
    };
    return bad(status.low_half.state) || bad(status.high_half.state);
}
} // namespace

bool valid_runtime_config(const RuntimeConfig& c) noexcept {
    return (c.left_target == PackedHalf::low || c.left_target == PackedHalf::high)
           && (c.left_feedback == PackedHalf::low || c.left_feedback == PackedHalf::high)
           && (c.left_sign == 1 || c.left_sign == -1) && (c.right_sign == 1 || c.right_sign == -1) && c.max_abs_rpm > 0
           && c.max_abs_rpm <= 1000 && c.standstill_tenths_rpm >= 0 && c.standstill_tenths_rpm <= 10
           && c.heartbeat_timeout > time::Duration::zero() && c.feedback_timeout > time::Duration::zero()
           && c.decision_timeout > time::Duration::zero();
}

RuntimePolicy::RuntimePolicy(RuntimeConfig config) noexcept : config_{config} {
    if (!valid_runtime_config(config_)) {
        inhibit(RuntimeReason::config_invalid);
        state_.stopped = true;
    }
}

void RuntimePolicy::inhibit(RuntimeReason reason) noexcept {
    if (state_.armed || state_.output.reason != reason) {
        if (state_.epoch == std::numeric_limits<std::uint64_t>::max()) {
            state_.stopped = true;
            reason = RuntimeReason::exhausted;
        } else {
            ++state_.epoch;
        }
    }
    state_.armed = false;
    state_.output = {};
    state_.output.reason = reason;
}

void RuntimePolicy::stop(RuntimeReason reason) noexcept {
    inhibit(reason);
    state_.stopped = true;
}

RuntimeState RuntimePolicy::state() const noexcept {
    return state_;
}

bool RuntimePolicy::advance(time::MonotonicTime now) noexcept {
    if (state_.stopped) {
        return false;
    }
    if (now < last_now_ || now.time_since_epoch() < time::Duration::zero()) {
        inhibit(RuntimeReason::clock_invalid);
        state_.stopped = true;
        return false;
    }
    last_now_ = now;
    return true;
}

void RuntimePolicy::observe(const RuntimeFeedback& f, time::MonotonicTime now) noexcept {
    if (!advance(now)) {
        return;
    }
    const auto previous = state_.feedback;
    if (previous.generation.transport != 0 && f.generation != previous.generation) {
        inhibit(RuntimeReason::generation_changed);
    }
    const bool replay =
        f.version == 0 || f.version < previous.version || f.generation.transport < previous.generation.transport
        || (f.generation.transport == previous.generation.transport && f.generation.boot < previous.generation.boot);
    if (replay) {
        state_.healthy = false;
        inhibit(RuntimeReason::feedback_invalid);
        return;
    }
    state_.feedback = f;
    const auto velocity = zlac8015d::decode_packed_velocity(bytes(f.velocity_raw));
    if (!velocity) {
        state_.healthy = false;
        inhibit(RuntimeReason::feedback_invalid);
        return;
    }
    const auto low = static_cast<std::int32_t>(velocity->low_half_tenths_rpm);
    const auto high = static_cast<std::int32_t>(velocity->high_half_tenths_rpm);
    state_.left_tenths_rpm = (config_.left_feedback == PackedHalf::low ? low : high) * config_.left_sign;
    state_.right_tenths_rpm = (config_.left_feedback == PackedHalf::low ? high : low) * config_.right_sign;
    const auto status = decode_dual_axis_status(f.status_raw);
    state_.healthy = !replay && f.current && f.operational && f.generation.transport != 0
                     && fresh(f.heartbeat_at, now, config_.heartbeat_timeout)
                     && fresh(f.status_at, now, config_.feedback_timeout)
                     && fresh(f.diagnostics_at, now, config_.feedback_timeout) && f.mode_raw == 3 && f.fault_raw == 0
                     && !bad_status(status);
    if (state_.armed && state_.healthy && f.status_at > zero_at_ && stationary(state_, config_.standstill_tenths_rpm)) {
        zero_confirmed_ = true;
    }
    if (!state_.healthy || (state_.armed && enabled_seen_ && !velocity_motion_eligible(status, f.mode_raw))) {
        inhibit(RuntimeReason::feedback_invalid);
    } else if (state_.armed && !fresh(last_request_at_, now, config_.decision_timeout)) {
        inhibit(RuntimeReason::decision_expired);
    }
    if (state_.armed && state_.output.payload[0] == std::byte{15} && velocity_motion_eligible(status, f.mode_raw)) {
        enabled_seen_ = true;
    }
}

RuntimeOutput RuntimePolicy::publish(TransitionControlword word, command::MotionCommand command,
                                     RuntimeReason reason) noexcept {
    const auto control = zlac8015d::encode_transition_controlword(word);
    const auto left = std::clamp(command.left_rpm, -config_.max_abs_rpm, config_.max_abs_rpm);
    const auto right = std::clamp(command.right_rpm, -config_.max_abs_rpm, config_.max_abs_rpm);
    const auto left_bytes =
        zlac8015d::encode_independent_target(zlac8015d::IndependentChannel::subindex_1, left * config_.left_sign);
    const auto right_bytes =
        zlac8015d::encode_independent_target(zlac8015d::IndependentChannel::subindex_2, right * config_.right_sign);
    if (!control || !left_bytes || !right_bytes) {
        inhibit(RuntimeReason::config_invalid);
        return state_.output;
    }
    const auto& low = config_.left_target == PackedHalf::low ? *left_bytes : *right_bytes;
    const auto& high = config_.left_target == PackedHalf::low ? *right_bytes : *left_bytes;
    state_.output = {.payload = {(*control)[0], (*control)[1], low[0], low[1], high[0], high[1]},
                     .command = {left, right, command.stop_requested},
                     .reason = reason,
                     .accepted = true};
    if (word == TransitionControlword::enable_operation
        && velocity_motion_eligible(decode_dual_axis_status(state_.feedback.status_raw), state_.feedback.mode_raw)) {
        enabled_seen_ = true;
    }
    return state_.output;
}

RuntimeOutput RuntimePolicy::evaluate(const RuntimeRequest& r, time::MonotonicTime now) noexcept {
    observe(state_.feedback, now);
    if (state_.stopped) {
        return state_.output;
    }
    const auto reject = [&](RuntimeReason why) {
        inhibit(why);
        return state_.output;
    };
    const auto& d = r.decision;
    if (d.decision_generation == std::numeric_limits<std::uint64_t>::max()
        || r.authorization == std::numeric_limits<std::uint64_t>::max()) {
        state_.stopped = true;
        return reject(RuntimeReason::exhausted);
    }
    if (d.decision_generation == 0 || d.decision_generation <= last_decision_) {
        return reject(RuntimeReason::decision_invalid);
    }
    last_decision_ = d.decision_generation;
    if (r.generation != state_.feedback.generation || r.feedback_epoch != state_.epoch
        || !fresh(r.issued_at, now, config_.decision_timeout)) {
        highest_authorization_ = std::max(highest_authorization_, r.authorization);
        return reject(RuntimeReason::decision_invalid);
    }
    const bool terminal = d.state == safety::SafetyState::shutdown;
    if (terminal || d.action == DriveAction::quick_stop || d.action == DriveAction::disable_voltage
        || d.action == DriveAction::hold_disabled || d.action == DriveAction::inhibit) {
        highest_authorization_ = std::max(highest_authorization_, r.authorization);
        inhibit(terminal ? RuntimeReason::shutdown : RuntimeReason::rearm_required);
        const auto word = d.action == DriveAction::quick_stop ? TransitionControlword::quick_stop
                                                              : TransitionControlword::disable_voltage;
        auto out = publish(word, {}, state_.output.reason);
        if (terminal) {
            state_.stopped = true;
        }
        return out;
    }
    if (!state_.healthy) {
        highest_authorization_ = std::max(highest_authorization_, r.authorization);
        return reject(RuntimeReason::feedback_invalid);
    }
    const bool zero = d.approved_command.left_rpm == 0 && d.approved_command.right_rpm == 0;
    const bool transition = d.action == DriveAction::shutdown || d.action == DriveAction::switch_on
                            || d.action == DriveAction::enable_operation;
    const bool compatible_state = d.state == safety::SafetyState::zero_hold || d.state == safety::SafetyState::normal;
    if (!compatible_state || (!transition && d.action != DriveAction::approved_target)
        || (d.authorization_generation != 0 && d.authorization_generation != r.authorization)
        || (transition && (!zero || d.motion_approved))) {
        highest_authorization_ = std::max(highest_authorization_, r.authorization);
        return reject(RuntimeReason::decision_invalid);
    }
    if (r.authorization > highest_authorization_) {
        highest_authorization_ = r.authorization;
        if (!zero || !transition || !stationary(state_, config_.standstill_tenths_rpm)) {
            return reject(RuntimeReason::zero_required);
        }
        state_.authorization = r.authorization;
        state_.armed = true;
        zero_at_ = now;
        zero_confirmed_ = false;
        enabled_seen_ = false;
        last_request_at_ = r.issued_at;
        return publish(TransitionControlword::shutdown, {}, RuntimeReason::zero_required);
    }
    if (!state_.armed || r.authorization == 0 || r.authorization != state_.authorization) {
        return reject(RuntimeReason::rearm_required);
    }
    const auto status = decode_dual_axis_status(state_.feedback.status_raw);
    const bool newer_zero = zero_confirmed_;
    const auto both = [&](Cia402State expected) {
        return status.low_half.state == expected && status.high_half.state == expected;
    };
    if (!newer_zero) {
        return reject(RuntimeReason::zero_required);
    }
    last_request_at_ = r.issued_at;
    if (transition) {
        // A normal zero command may decelerate an already enabled drive.
        if (d.action == DriveAction::enable_operation && both(Cia402State::operation_enabled)) {
            return publish(TransitionControlword::enable_operation, {}, RuntimeReason::none);
        }
        if (!stationary(state_, config_.standstill_tenths_rpm)) {
            return reject(RuntimeReason::zero_required);
        }
        if (d.action == DriveAction::shutdown) {
            if (enabled_seen_) {
                inhibit(RuntimeReason::rearm_required);
            }
            return publish(TransitionControlword::shutdown, {}, RuntimeReason::none);
        }
        if (d.action == DriveAction::switch_on
            && (both(Cia402State::ready_to_switch_on) || both(Cia402State::switched_on))) {
            return publish(TransitionControlword::switch_on, {}, RuntimeReason::none);
        }
        if (d.action == DriveAction::enable_operation
            && (both(Cia402State::switched_on) || both(Cia402State::operation_enabled))) {
            return publish(TransitionControlword::enable_operation, {}, RuntimeReason::none);
        }
        return reject(RuntimeReason::transition_invalid);
    }
    if (!d.motion_approved || d.state != safety::SafetyState::normal || d.approved_command.stop_requested
        || d.authorization_generation != r.authorization
        || !velocity_motion_eligible(status, state_.feedback.mode_raw)) {
        return reject(RuntimeReason::decision_invalid);
    }
    return publish(TransitionControlword::enable_operation, d.approved_command, RuntimeReason::none);
}
} // namespace robot_control::domain::drive
