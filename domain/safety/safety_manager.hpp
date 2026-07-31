#pragma once

#include "domain/control/control_arbiter.hpp"
#include "domain/drive/cia402.hpp"

#include <cstdint>

namespace robot_control::domain::safety {

enum class SafetyState : std::uint8_t {
  startup_inhibit = 0,
  zero_hold,
  normal,
  remote_lost,
  external_command_lost,
  canopen_unavailable,
  drive_fault,
  emergency_stop,
  shutdown,
};

enum class DriveAction : std::uint8_t {
  inhibit = 0,
  hold_disabled,
  shutdown,
  switch_on,
  enable_operation,
  approved_target,
  quick_stop,
  disable_voltage,
  fault_reset,
};

struct SafetyInput {
  control::SelectedCommand selected{};
  drive::Cia402State low_half_state{drive::Cia402State::unknown};
  drive::Cia402State high_half_state{drive::Cia402State::unknown};
  std::uint64_t system_authorization_generation{0};
  std::uint64_t fault_reset_generation{0};
  bool coherent{false};
  bool can_started{false};
  bool can_bus_off{false};
  bool nmt_operational{false};
  bool heartbeat_fresh{false};
  bool status_fresh{false};
  bool required_tpdo_fresh{false};
  bool mode_valid{false};
  bool drive_fault_active{false};
  bool fault_cause_absent{false};
  bool emergency_stop_known{false};
  bool emergency_stop_active{false};
  bool shutdown_requested{false};
  bool fault_reset_requested{false};
};

struct SafetyDecision {
  SafetyState state{SafetyState::startup_inhibit};
  DriveAction action{DriveAction::inhibit};
  command::MotionCommand approved_command{};
  std::uint64_t authorization_generation{0};
  std::uint64_t decision_generation{0};
  bool motion_approved{false};
  bool fault_reset_eligible{false};
};

class SafetyManager final {
public:
  /**
   * Construct the manager in startup inhibit with no authorization.
   *
   * Thread safety: Instances are single-owner and not internally synchronized.
   */
  SafetyManager() noexcept = default;

  /**
   * Evaluate one coherent safety snapshot.
   *
   * @param input Complete domain observation for one control cycle.
   * @return Typed fail-closed drive action and optional approved target.
   *
   * Thread safety: Single-owner only.
   */
  [[nodiscard]] SafetyDecision evaluate(const SafetyInput &input) noexcept;

private:
  /** Return whether every communication and feedback prerequisite is valid. */
  [[nodiscard]] static bool
  communication_ready(const SafetyInput &input) noexcept;
  /** Return whether both neutral status halves equal one expected state. */
  [[nodiscard]] static bool both_axes(const SafetyInput &input,
                                      drive::Cia402State state) noexcept;

  std::uint64_t last_authorization_generation_{0};
  std::uint64_t last_fault_reset_generation_{0};
  std::uint64_t decision_generation_{0};
  bool rearm_required_{true};
};

} // namespace robot_control::domain::safety
