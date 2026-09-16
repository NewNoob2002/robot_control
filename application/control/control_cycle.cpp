#include "application/control/control_cycle.hpp"

#include <limits>

namespace robot_control::application::control {

bool valid_cycle_config(const CycleConfig& config) noexcept {
    return drive::valid_runtime_config(config.runtime) && config.period > time::Duration::zero()
           && config.period < config.runtime.decision_timeout && config.arbiter.sbus_timeout > time::Duration::zero()
           && config.arbiter.external_timeout > time::Duration::zero()
           && config.arbiter.handover_zero_dwell >= time::Duration::zero() && config.arbiter.max_abs_rpm > 0
           && config.arbiter.max_abs_rpm <= config.runtime.max_abs_rpm
           && config.transition_timeout > time::Duration::zero();
}

ControlCycle::ControlCycle(CycleConfig config) noexcept : config_{config}, arbiter_{config.arbiter} {
    stopped_ = !valid_cycle_config(config);
}

std::size_t ControlCycle::index(command::Source source) noexcept {
    return source == command::Source::external ? 1U : 0U;
}

void ControlCycle::consume(Authority authority) noexcept {
    if (authority.source == command::Source::none)
        return;
    auto& floor = consumed_[index(authority.source)];
    if (authority.session > floor.session
        || (authority.session == floor.session && authority.generation > floor.generation))
        floor = authority;
}

bool ControlCycle::fresh(Authority authority) const noexcept {
    const auto& floor = consumed_[index(authority.source)];
    return authority.source != command::Source::none && authority.session != 0 && authority.generation != 0
           && (authority.session > floor.session
               || (authority.session == floor.session && authority.generation > floor.generation));
}

void ControlCycle::revoke() noexcept {
    consume(active_);
    consume(pending_);
    active_ = {};
    pending_ = {};
    expected_ = drive::Cia402State::unknown;
}

CycleResult ControlCycle::prepare(const CycleInput& input, const drive::RuntimeState& state,
                                  time::MonotonicTime now) noexcept {
    if (now < time::MonotonicTime{} || (have_tick_ && now < last_tick_) || awaiting_decision_ != 0)
        stopped_ = true;
    if (runtime_epoch_ != state.epoch || state.stopped) {
        revoke();
        runtime_epoch_ = state.epoch;
    }
    stopped_ = stopped_ || state.stopped;
    // Period is a scheduling contract, not a claim of a real-time scheduler.
    const bool cycle_gap = have_tick_ && now >= last_tick_ && now - last_tick_ >= config_.runtime.decision_timeout;
    last_tick_ = now;
    have_tick_ = true;
    stopped_ = stopped_ || input.shutdown_requested;
    const domain::control::ArbiterInput commands{input.sbus, input.external, input.coherent};
    CycleResult result;
    result.selected = arbiter_.evaluate(commands, now);
    const auto status = drive::decode_dual_axis_status(state.feedback.status_raw);
    const Authority candidate{result.selected.source, result.selected.source_session_generation,
                              result.selected.authorization_generation};
    const bool stationary = state.left_tenths_rpm >= -config_.runtime.standstill_tenths_rpm
                            && state.left_tenths_rpm <= config_.runtime.standstill_tenths_rpm
                            && state.right_tenths_rpm >= -config_.runtime.standstill_tenths_rpm
                            && state.right_tenths_rpm <= config_.runtime.standstill_tenths_rpm;
    const Authority interlock{command::Source::sbus, commands.sbus.session_generation,
                              commands.sbus.authorization_generation};
    const bool interlock_changed =
        (active_.source == command::Source::external || pending_.source == command::Source::external)
        && interlock != sbus_interlock_;
    sbus_interlock_ = interlock;
    const bool safe = input.coherent && input.emergency_stop_known && !input.emergency_stop_active && state.healthy
                      && !stopped_ && !cycle_gap && !interlock_changed;
    bool transition_failed = false;
    if (expected_ != drive::Cia402State::unknown) {
        if (now - transition_since_ >= config_.transition_timeout) {
            transition_failed = true;
        } else if (state.feedback.status_at > transition_status_at_ && status.low_half.state == expected_
                   && status.high_half.state == expected_) {
            expected_ = drive::Cia402State::unknown;
        }
    }
    if (!safe || !result.selected.valid || transition_failed) {
        revoke();
        consume({command::Source::sbus, commands.sbus.session_generation, commands.sbus.authorization_generation});
        consume({command::Source::external, commands.external.session_generation,
                 commands.external.authorization_generation});
    } else {
        if (active_.source != command::Source::none && active_ != candidate)
            revoke();
        if (active_.source == command::Source::none) {
            if (pending_.source != command::Source::none && pending_ != candidate)
                revoke();
            if (!result.selected.command.is_zero() || !stationary) {
                consume(candidate);
                revoke();
            } else if (fresh(candidate)) {
                if (pending_.source == command::Source::none) {
                    pending_ = candidate;
                    zero_since_ = now;
                }
                if (now - zero_since_ >= config_.arbiter.handover_zero_dwell) {
                    if (authorization_ >= std::numeric_limits<std::uint64_t>::max() - 1) {
                        stopped_ = true;
                        revoke();
                    } else {
                        ++authorization_;
                        active_ = candidate;
                        pending_ = {};
                    }
                }
            }
        }
    }
    auto selected = result.selected;
    if (active_.source == command::Source::none) {
        selected.valid = false;
        selected.command = {};
    }
    // While enabling, commands remain zero even if the stick leaves neutral.
    if (!state.armed || expected_ != drive::Cia402State::unknown
        || status.low_half.state != drive::Cia402State::operation_enabled
        || status.high_half.state != drive::Cia402State::operation_enabled)
        selected.command = {};
    /** Preserve distinct communication and drive-fault diagnostics. */
    const auto feedback_fresh = [now](time::MonotonicTime stamp, time::Duration timeout) {
        return stamp >= time::MonotonicTime{} && stamp <= now && now - stamp < timeout;
    };
    safety::SafetyInput safety_input{
        .selected = selected,
        .low_half_state = status.low_half.state,
        .high_half_state = status.high_half.state,
        .system_authorization_generation = authorization_,
        .coherent = input.coherent,
        .can_started = state.feedback.current,
        .nmt_operational = state.feedback.operational,
        .heartbeat_fresh = feedback_fresh(state.feedback.heartbeat_at, config_.runtime.heartbeat_timeout),
        .status_fresh = feedback_fresh(state.feedback.status_at, config_.runtime.feedback_timeout),
        .required_tpdo_fresh = feedback_fresh(state.feedback.diagnostics_at, config_.runtime.feedback_timeout),
        .mode_valid = state.feedback.mode_raw == 3,
        .drive_fault_active = state.feedback.fault_raw != 0,
        .emergency_stop_known = input.emergency_stop_known,
        .emergency_stop_active = input.emergency_stop_active,
        .shutdown_requested = stopped_,
    };
    result.request = {.decision = safety_.evaluate(safety_input),
                      .generation = state.feedback.generation,
                      .feedback_epoch = state.epoch,
                      .authorization = authorization_,
                      .issued_at = now};
    if (active_.source != command::Source::none && expected_ != drive::Cia402State::unknown) {
        result.request.decision.action =
            (expected_ == drive::Cia402State::ready_to_switch_on || expected_ == drive::Cia402State::switch_on_disabled)
                ? safety::DriveAction::shutdown
                : (expected_ == drive::Cia402State::switched_on ? safety::DriveAction::switch_on
                                                                : safety::DriveAction::enable_operation);
        result.request.decision.approved_command = {};
        result.request.decision.motion_approved = false;
    }
    awaiting_decision_ = result.request.decision.decision_generation;
    return result;
}

void ControlCycle::complete(CycleResult& result, const drive::RuntimeState& after, time::MonotonicTime now) noexcept {
    if (awaiting_decision_ == 0 || result.request.decision.decision_generation != awaiting_decision_
        || result.request.issued_at != last_tick_ || now < last_tick_) {
        stopped_ = true;
        revoke();
        result.output = {};
        result.output.reason = drive::RuntimeReason::decision_invalid;
        return;
    }
    awaiting_decision_ = 0;
    result.output = after.output;
    stopped_ = stopped_ || after.stopped;
    // Expected safe zero publications can change the runtime epoch during dwell.
    // A rejected command or lost armed state retires the authorization instead.
    if (!result.output.accepted || (active_.source != command::Source::none && !after.armed))
        revoke();
    runtime_epoch_ = after.epoch;
    if (active_.source != command::Source::none && expected_ == drive::Cia402State::unknown) {
        const auto word = result.output.payload[0];
        const auto status = drive::decode_dual_axis_status(after.feedback.status_raw);
        auto next = drive::Cia402State::unknown;
        if (word == std::byte{0})
            next = drive::Cia402State::switch_on_disabled;
        if (word == std::byte{6})
            next = drive::Cia402State::ready_to_switch_on;
        if (word == std::byte{7})
            next = drive::Cia402State::switched_on;
        if (word == std::byte{15}
            && (status.low_half.state != drive::Cia402State::operation_enabled
                || status.high_half.state != drive::Cia402State::operation_enabled))
            next = drive::Cia402State::operation_enabled;
        if (next != drive::Cia402State::unknown) {
            expected_ = next;
            transition_since_ = now;
            transition_status_at_ = after.feedback.status_at;
        }
    }
}
} // namespace robot_control::application::control
