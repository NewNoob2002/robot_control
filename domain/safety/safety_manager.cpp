#include "domain/safety/safety_manager.hpp"

namespace robot_control::domain::safety {
namespace {

/**
 * Construct a zero-target inhibited safety decision.
 *
 * @param state Safety state to report.
 * @param action Safe drive action.
 * @param generation Decision generation.
 * @return Fail-closed decision.
 */
SafetyDecision inhibited(const SafetyState state, const DriveAction action,
                         const std::uint64_t generation) noexcept {
  return SafetyDecision{
      .state = state,
      .action = action,
      .decision_generation = generation,
  };
}

} // namespace

bool SafetyManager::communication_ready(const SafetyInput &input) noexcept {
  return input.coherent && input.can_started && !input.can_bus_off &&
         input.nmt_operational && input.heartbeat_fresh && input.status_fresh &&
         input.required_tpdo_fresh && input.mode_valid;
}

bool SafetyManager::both_axes(const SafetyInput &input,
                              const drive::Cia402State state) noexcept {
  return input.low_half_state == state && input.high_half_state == state;
}

SafetyDecision SafetyManager::evaluate(const SafetyInput &input) noexcept {
  ++decision_generation_;
  if (input.shutdown_requested) {
    rearm_required_ = true;
    return inhibited(SafetyState::shutdown, DriveAction::disable_voltage,
                     decision_generation_);
  }
  if (!input.coherent) {
    rearm_required_ = true;
    return inhibited(SafetyState::startup_inhibit, DriveAction::hold_disabled,
                     decision_generation_);
  }
  if (!input.emergency_stop_known || input.emergency_stop_active) {
    rearm_required_ = true;
    return inhibited(SafetyState::emergency_stop, DriveAction::disable_voltage,
                     decision_generation_);
  }
  if (!communication_ready(input)) {
    rearm_required_ = true;
    return inhibited(SafetyState::canopen_unavailable,
                     input.can_started && !input.can_bus_off
                         ? DriveAction::quick_stop
                         : DriveAction::hold_disabled,
                     decision_generation_);
  }
  if (input.drive_fault_active || both_axes(input, drive::Cia402State::fault)) {
    rearm_required_ = true;
    const bool fresh_reset =
        input.fault_reset_requested && input.fault_cause_absent &&
        input.selected.command.is_zero() && input.selected.valid &&
        input.fault_reset_generation > last_fault_reset_generation_ &&
        both_axes(input, drive::Cia402State::fault);
    if (fresh_reset) {
      last_fault_reset_generation_ = input.fault_reset_generation;
      return SafetyDecision{
          .state = SafetyState::drive_fault,
          .action = DriveAction::fault_reset,
          .authorization_generation = input.system_authorization_generation,
          .decision_generation = decision_generation_,
          .motion_approved = false,
          .fault_reset_eligible = true,
      };
    }
    return inhibited(SafetyState::drive_fault, DriveAction::quick_stop,
                     decision_generation_);
  }
  if (!input.selected.valid || input.selected.source == command::Source::none) {
    rearm_required_ = true;
    return inhibited(SafetyState::zero_hold, DriveAction::quick_stop,
                     decision_generation_);
  }

  const bool fresh_authorization =
      input.system_authorization_generation > last_authorization_generation_;
  if (rearm_required_ && !fresh_authorization) {
    return inhibited(SafetyState::zero_hold, DriveAction::quick_stop,
                     decision_generation_);
  }

  if (both_axes(input, drive::Cia402State::switch_on_disabled)) {
    return inhibited(SafetyState::zero_hold, DriveAction::shutdown,
                     decision_generation_);
  }
  if (both_axes(input, drive::Cia402State::ready_to_switch_on)) {
    return inhibited(SafetyState::zero_hold, DriveAction::switch_on,
                     decision_generation_);
  }
  if (both_axes(input, drive::Cia402State::switched_on)) {
    return inhibited(SafetyState::zero_hold, DriveAction::enable_operation,
                     decision_generation_);
  }
  if (!both_axes(input, drive::Cia402State::operation_enabled)) {
    rearm_required_ = true;
    return inhibited(SafetyState::zero_hold, DriveAction::quick_stop,
                     decision_generation_);
  }

  if (fresh_authorization) {
    last_authorization_generation_ = input.system_authorization_generation;
  }
  rearm_required_ = false;
  return SafetyDecision{
      .state = SafetyState::normal,
      .action = input.selected.command.is_zero() ? DriveAction::enable_operation
                                                 : DriveAction::approved_target,
      .approved_command = input.selected.command,
      .authorization_generation = input.system_authorization_generation,
      .decision_generation = decision_generation_,
      .motion_approved = !input.selected.command.is_zero(),
      .fault_reset_eligible = false,
  };
}

} // namespace robot_control::domain::safety
