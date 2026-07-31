#include "domain/control/control_arbiter.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace robot_control::domain::control {
namespace {

/**
 * Convert a typed arbiter reason to its diagnostic bit representation.
 *
 * @param reason Reason to encode.
 * @return Underlying diagnostic bit.
 */
constexpr std::uint32_t flag(const ArbiterReason reason) noexcept {
  return static_cast<std::uint32_t>(reason);
}

} // namespace

ControlArbiter::ControlArbiter(const ArbiterConfig config) noexcept
    : config_{config} {
  if (config_.max_abs_rpm < 0) {
    config_.max_abs_rpm = 0;
  }
}

bool ControlArbiter::sample_usable(const command::CommandSample &sample,
                                   const command::Source expected,
                                   const time::Duration timeout,
                                   const time::MonotonicTime now,
                                   ProducerHistory &history,
                                   std::uint32_t &reasons) noexcept {
  if (!command::structurally_valid(sample, expected) ||
      !time::is_fresh(now, sample.captured_at, timeout)) {
    reasons |= flag(expected == command::Source::sbus
                        ? ArbiterReason::sbus_unavailable
                        : ArbiterReason::external_unavailable);
    return false;
  }

  if (sample.session_generation < history.session_generation ||
      (history.session_generation == sample.session_generation &&
       sample.sequence < history.sequence)) {
    reasons |= flag(ArbiterReason::sequence_replay);
    return false;
  }

  if (history.session_generation < sample.session_generation) {
    history.session_generation = sample.session_generation;
    history.sequence = sample.sequence;
  } else {
    history.sequence = std::max(history.sequence, sample.sequence);
  }
  return true;
}

void ControlArbiter::update_zero_tracking(
    const ArbiterInput &input, const bool sbus_usable,
    const bool external_usable, const time::MonotonicTime now) noexcept {
  const bool both_zero = sbus_usable && external_usable &&
                         input.sbus.command.is_zero() &&
                         input.external.command.is_zero();
  if (!both_zero) {
    both_zero_tracking_ = false;
    return;
  }
  if (!both_zero_tracking_) {
    both_zero_tracking_ = true;
    both_zero_since_ = now;
  }
}

SelectedCommand
ControlArbiter::inhibited(const time::MonotonicTime now,
                          const std::uint32_t reasons) const noexcept {
  return SelectedCommand{
      .source = command::Source::none,
      .command = {},
      .selected_at = now,
      .handover_generation = handover_generation_,
      .reason_flags = reasons,
      .valid = false,
  };
}

command::MotionCommand ControlArbiter::bounded(
    const command::MotionCommand &requested) const noexcept {
  const auto limit = config_.max_abs_rpm;
  return command::MotionCommand{
      .left_rpm = std::clamp(requested.left_rpm, -limit, limit),
      .right_rpm = std::clamp(requested.right_rpm, -limit, limit),
      .stop_requested = requested.stop_requested,
  };
}

SelectedCommand
ControlArbiter::evaluate(const ArbiterInput &input,
                         const time::MonotonicTime now) noexcept {
  if (!input.coherent) {
    external_recovery_required_ = true;
    required_external_authorization_generation_ =
        std::max(required_external_authorization_generation_,
                 input.external.authorization_generation);
    ++handover_generation_;
    return inhibited(now, flag(ArbiterReason::input_incoherent));
  }

  std::uint32_t reasons = 0;
  const bool sbus_usable =
      sample_usable(input.sbus, command::Source::sbus, config_.sbus_timeout,
                    now, sbus_history_, reasons);
  const bool external_usable =
      sample_usable(input.external, command::Source::external,
                    config_.external_timeout, now, external_history_, reasons);

  const bool zero_was_qualified =
      both_zero_tracking_ &&
      (now - both_zero_since_) >= config_.handover_zero_dwell;
  update_zero_tracking(input, sbus_usable, external_usable, now);

  if (sbus_usable && !input.sbus.command.is_zero()) {
    if (!manual_was_authoritative_) {
      ++handover_generation_;
    }
    manual_was_authoritative_ = true;
    external_recovery_required_ = true;
    required_external_authorization_generation_ =
        std::max(required_external_authorization_generation_,
                 input.external.authorization_generation);
    return SelectedCommand{
        .source = command::Source::sbus,
        .command = bounded(input.sbus.command),
        .selected_at = now,
        .source_session_generation = input.sbus.session_generation,
        .authorization_generation = input.sbus.authorization_generation,
        .handover_generation = handover_generation_,
        .reason_flags = reasons,
        .valid = true,
    };
  }

  if (!sbus_usable || !external_usable) {
    external_recovery_required_ = true;
    required_external_authorization_generation_ =
        std::max(required_external_authorization_generation_,
                 input.external.authorization_generation);
    if (manual_was_authoritative_) {
      ++handover_generation_;
      manual_was_authoritative_ = false;
    }
    return inhibited(now, reasons | flag(ArbiterReason::no_valid_source));
  }

  if (input.external.command.is_zero()) {
    return inhibited(now, reasons | flag(ArbiterReason::zero_qualifying));
  }

  if (external_recovery_required_ && !zero_was_qualified) {
    return inhibited(now, reasons | flag(ArbiterReason::rearm_required));
  }

  if (input.external.authorization_generation <=
      std::max(last_external_authorization_generation_,
               required_external_authorization_generation_)) {
    return inhibited(now, reasons | flag(ArbiterReason::rearm_replay));
  }

  last_external_authorization_generation_ =
      input.external.authorization_generation;
  external_recovery_required_ = false;
  manual_was_authoritative_ = false;
  return SelectedCommand{
      .source = command::Source::external,
      .command = bounded(input.external.command),
      .selected_at = now,
      .source_session_generation = input.external.session_generation,
      .authorization_generation = input.external.authorization_generation,
      .handover_generation = handover_generation_,
      .reason_flags = reasons,
      .valid = true,
  };
}

std::uint64_t ControlArbiter::handover_generation() const noexcept {
  return handover_generation_;
}

} // namespace robot_control::domain::control
