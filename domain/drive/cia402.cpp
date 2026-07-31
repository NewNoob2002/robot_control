#include "domain/drive/cia402.hpp"

namespace robot_control::domain::drive {

AxisStatus decode_statusword(const std::uint16_t raw) noexcept {
  constexpr std::uint16_t state_mask = 0x006FU;
  const auto masked = static_cast<std::uint16_t>(raw & state_mask);
  Cia402State state = Cia402State::unknown;
  switch (masked) {
  case 0x0000U:
    state = Cia402State::not_ready_to_switch_on;
    break;
  case 0x0040U:
    state = Cia402State::switch_on_disabled;
    break;
  case 0x0021U:
    state = Cia402State::ready_to_switch_on;
    break;
  case 0x0023U:
    state = Cia402State::switched_on;
    break;
  case 0x0027U:
    state = Cia402State::operation_enabled;
    break;
  case 0x0007U:
    state = Cia402State::quick_stop_active;
    break;
  case 0x000FU:
    state = Cia402State::fault_reaction_active;
    break;
  case 0x0008U:
    state = Cia402State::fault;
    break;
  default:
    break;
  }
  return AxisStatus{
      .state = state,
      .raw_statusword = raw,
      .non_state_bits = static_cast<std::uint16_t>(raw & ~state_mask),
  };
}

DualAxisStatus decode_dual_axis_status(const std::uint32_t raw) noexcept {
  return DualAxisStatus{
      .low_half = decode_statusword(static_cast<std::uint16_t>(raw)),
      .high_half = decode_statusword(static_cast<std::uint16_t>(raw >> 16U)),
      .raw_status = raw,
      .physical_axis_mapping_known = false,
  };
}

bool velocity_motion_eligible(const DualAxisStatus &status,
                              const std::int8_t actual_mode,
                              const std::int8_t requested_mode) noexcept {
  return status.low_half.state == Cia402State::operation_enabled &&
         status.high_half.state == Cia402State::operation_enabled &&
         actual_mode == requested_mode;
}

void TransitionTracker::start(const Cia402State expected,
                              const std::uint64_t observation_generation,
                              const time::MonotonicTime now,
                              const time::Duration timeout) noexcept {
  expected_ = expected;
  command_observation_generation_ = observation_generation;
  deadline_ = now + (timeout < time::Duration::zero() ? time::Duration::zero()
                                                      : timeout);
  pending_ = true;
}

TransitionResult
TransitionTracker::evaluate(const Cia402State actual,
                            const std::uint64_t observation_generation,
                            const time::MonotonicTime now) noexcept {
  if (!pending_) {
    return TransitionResult::idle;
  }
  if (now >= deadline_) {
    pending_ = false;
    return TransitionResult::timed_out;
  }
  if (observation_generation <= command_observation_generation_) {
    return TransitionResult::observation_replayed;
  }
  if (actual == expected_) {
    pending_ = false;
    return TransitionResult::reached;
  }
  return TransitionResult::pending;
}

} // namespace robot_control::domain::drive
