#include "domain/control/control_arbiter.hpp"
#include "domain/drive/cia402.hpp"
#include "domain/safety/safety_manager.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

using namespace std::chrono_literals;
using robot_control::domain::command::CommandSample;
using robot_control::domain::command::MotionCommand;
using robot_control::domain::command::Source;
using robot_control::domain::control::ArbiterConfig;
using robot_control::domain::control::ArbiterInput;
using robot_control::domain::control::ControlArbiter;
using robot_control::domain::drive::Cia402State;
using robot_control::domain::drive::TransitionResult;
using robot_control::domain::drive::TransitionTracker;
using robot_control::domain::safety::DriveAction;
using robot_control::domain::safety::SafetyInput;
using robot_control::domain::safety::SafetyManager;
using robot_control::domain::safety::SafetyState;
using robot_control::domain::time::MonotonicTime;

int failures = 0;

/**
 * Record a failed test expression.
 *
 * @param condition Assertion condition.
 * @param id Stable behavior-vector identifier.
 * @param expression Human-readable assertion expression.
 */
void check(const bool condition, const std::string_view id,
           const std::string_view expression) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL id=" << id << " expression=\"" << expression << "\"\n";
  }
}

#define CHECK(id, expression) check((expression), (id), #expression)

/**
 * Create one structurally valid command sample for tests.
 *
 * @return Fully populated sample owned by the caller.
 */
CommandSample sample(const Source source, const MonotonicTime now,
                     const std::int32_t left, const std::int32_t right,
                     const std::uint64_t authorization,
                     const std::uint64_t sequence = 1) {
  return CommandSample{
      .source = source,
      .command =
          MotionCommand{
              .left_rpm = left,
              .right_rpm = right,
              .stop_requested = left == 0 && right == 0,
          },
      .captured_at = now,
      .session_generation = 1,
      .authorization_generation = authorization,
      .sequence = sequence,
      .valid = true,
      .coherent = true,
      .enabled = true,
  };
}

/**
 * Create one coherent SBUS/external input pair for tests.
 *
 * @return Fully populated arbiter input owned by the caller.
 */
ArbiterInput pair(const MonotonicTime now, const std::int32_t sbus,
                  const std::int32_t external,
                  const std::uint64_t external_authorization,
                  const std::uint64_t sequence = 1) {
  return ArbiterInput{
      .sbus = sample(Source::sbus, now, sbus, sbus, 1, sequence),
      .external = sample(Source::external, now, external, external,
                         external_authorization, sequence),
      .coherent = true,
  };
}

/**
 * Create a communication-qualified zero-command safety input.
 *
 * @return Safe baseline input owned by the caller.
 */
SafetyInput safe_input() {
  return SafetyInput{
      .selected =
          {
              .source = Source::external,
              .command = {.left_rpm = 0,
                          .right_rpm = 0,
                          .stop_requested = true},
              .authorization_generation = 1,
              .valid = true,
          },
      .low_half_state = Cia402State::operation_enabled,
      .high_half_state = Cia402State::operation_enabled,
      .system_authorization_generation = 1,
      .coherent = true,
      .can_started = true,
      .nmt_operational = true,
      .heartbeat_fresh = true,
      .status_fresh = true,
      .required_tpdo_fresh = true,
      .mode_valid = true,
      .fault_cause_absent = true,
      .emergency_stop_known = true,
  };
}

/** Exercise command arbitration behavior-lock vectors. */
void test_arbitration() {
  const MonotonicTime t0{};
  ControlArbiter arbiter{ArbiterConfig{}};

  auto result = arbiter.evaluate(pair(t0, 100, 200, 1), t0);
  CHECK("ARB-001", result.source == Source::sbus);
  CHECK("ARB-001", result.command.left_rpm == 100);

  auto lost = pair(t0 + 1ms, 0, 200, 1, 2);
  lost.sbus.valid = false;
  result = arbiter.evaluate(lost, t0 + 1ms);
  CHECK("ARB-002", result.source == Source::none);
  CHECK("ARB-002", result.command.is_zero());

  result = arbiter.evaluate(pair(t0 + 2ms, 0, 0, 2, 3), t0 + 2ms);
  CHECK("ARB-003-setup", result.source == Source::none);
  result = arbiter.evaluate(pair(t0 + 151ms, 0, 0, 2, 4), t0 + 151ms);
  CHECK("ARB-003", result.source == Source::none);
  result = arbiter.evaluate(pair(t0 + 152ms, 0, 0, 2, 5), t0 + 152ms);
  CHECK("ARB-004-setup", result.source == Source::none);
  result = arbiter.evaluate(pair(t0 + 153ms, 0, 200, 1, 6), t0 + 153ms);
  CHECK("ARB-004", result.source == Source::none);

  ControlArbiter fresh_arbiter{ArbiterConfig{}};
  static_cast<void>(fresh_arbiter.evaluate(pair(t0, 100, 0, 1), t0));
  static_cast<void>(
      fresh_arbiter.evaluate(pair(t0 + 1ms, 0, 0, 2, 2), t0 + 1ms));
  static_cast<void>(
      fresh_arbiter.evaluate(pair(t0 + 151ms, 0, 0, 2, 3), t0 + 151ms));
  result = fresh_arbiter.evaluate(pair(t0 + 152ms, 0, 200, 2, 4), t0 + 152ms);
  CHECK("ARB-005", result.source == Source::external);

  auto incoherent = pair(t0 + 154ms, 0, 200, 3, 7);
  incoherent.coherent = false;
  result = arbiter.evaluate(incoherent, t0 + 154ms);
  CHECK("ARB-006", result.source == Source::none);

  auto replay = pair(t0 + 155ms, 0, 0, 4, 2);
  result = arbiter.evaluate(replay, t0 + 155ms);
  CHECK("ARB-006", result.source == Source::none);

  ControlArbiter session_arbiter{ArbiterConfig{}};
  auto newer_session = pair(t0, 100, 0, 1);
  newer_session.sbus.session_generation = 2;
  static_cast<void>(session_arbiter.evaluate(newer_session, t0));
  auto session_replay = pair(t0 + 1ms, 100, 0, 1, 2);
  session_replay.sbus.session_generation = 1;
  result = session_arbiter.evaluate(session_replay, t0 + 1ms);
  CHECK("ARB-006-session", result.source == Source::none);

  ControlArbiter bounded_arbiter{
      ArbiterConfig{.max_abs_rpm = 1000},
  };
  auto over_limit = pair(t0, 2000, 0, 1);
  over_limit.sbus.command.right_rpm = -2000;
  result = bounded_arbiter.evaluate(over_limit, t0);
  CHECK("ARB-limit-left", result.command.left_rpm == 1000);
  CHECK("ARB-limit-right", result.command.right_rpm == -1000);
}

/** Exercise monotonic freshness and sample-structure invariants. */
void test_time_and_sample_contract() {
  using robot_control::domain::command::structurally_valid;
  using robot_control::domain::time::is_fresh;

  const MonotonicTime t0{};
  CHECK("TIME-001", is_fresh(t0 + 100ms, t0, 100ms));
  CHECK("TIME-002", !is_fresh(t0 + 101ms, t0, 100ms));
  CHECK("TIME-003", !is_fresh(t0, t0 + 1ms, 100ms));
  CHECK("TIME-004", !is_fresh(t0, t0, -1ms));

  auto valid = sample(Source::sbus, t0, 0, 0, 1);
  CHECK("SAMPLE-001", structurally_valid(valid, Source::sbus));
  valid.session_generation = 0;
  CHECK("SAMPLE-002", !structurally_valid(valid, Source::sbus));
}

/** Exercise safety startup, fault reset, loss, and rearm vectors. */
void test_safety() {
  SafetyManager manager;
  SafetyInput input{};
  auto decision = manager.evaluate(input);
  CHECK("SAFE-001", decision.state == SafetyState::startup_inhibit);
  CHECK("SAFE-001", !decision.motion_approved);

  input = safe_input();
  input.low_half_state = Cia402State::switch_on_disabled;
  input.high_half_state = Cia402State::switch_on_disabled;
  decision = manager.evaluate(input);
  CHECK("CIA-001", decision.action == DriveAction::shutdown);
  CHECK("CIA-001", !decision.motion_approved);

  input = safe_input();
  input.low_half_state = Cia402State::fault;
  input.high_half_state = Cia402State::fault;
  input.drive_fault_active = true;
  input.fault_reset_requested = true;
  input.selected.command = {
      .left_rpm = 1, .right_rpm = 0, .stop_requested = false};
  input.fault_reset_generation = 1;
  decision = manager.evaluate(input);
  CHECK("SAFE-002", !decision.fault_reset_eligible);

  input.selected.command = {};
  decision = manager.evaluate(input);
  CHECK("SAFE-003", decision.fault_reset_eligible);
  CHECK("SAFE-003", decision.action == DriveAction::fault_reset);

  SafetyManager recovery_manager;
  input = safe_input();
  decision = recovery_manager.evaluate(input);
  CHECK("SAFE-004-setup", decision.state == SafetyState::normal);
  input.heartbeat_fresh = false;
  decision = recovery_manager.evaluate(input);
  CHECK("SAFE-004", decision.state == SafetyState::canopen_unavailable);
  CHECK("SAFE-004", !decision.motion_approved);

  input = safe_input();
  input.system_authorization_generation = 1;
  input.selected.command = {
      .left_rpm = 100, .right_rpm = 100, .stop_requested = false};
  decision = recovery_manager.evaluate(input);
  CHECK("SAFE-005", !decision.motion_approved);
  input.system_authorization_generation = 2;
  decision = recovery_manager.evaluate(input);
  CHECK("SAFE-005-new-generation", decision.motion_approved);
}

/** Exercise CiA402 decoding, mode eligibility, and transition timeout vectors.
 */
void test_cia402() {
  using robot_control::domain::drive::decode_dual_axis_status;
  using robot_control::domain::drive::decode_statusword;
  using robot_control::domain::drive::velocity_motion_eligible;

  CHECK("CIA-state-0",
        decode_statusword(0x0000).state == Cia402State::not_ready_to_switch_on);
  CHECK("CIA-001",
        decode_statusword(0x0040).state == Cia402State::switch_on_disabled);
  CHECK("CIA-state-21",
        decode_statusword(0x0021).state == Cia402State::ready_to_switch_on);
  CHECK("CIA-state-23",
        decode_statusword(0x0023).state == Cia402State::switched_on);
  CHECK("CIA-state-27",
        decode_statusword(0x0027).state == Cia402State::operation_enabled);
  CHECK("CIA-state-07",
        decode_statusword(0x0007).state == Cia402State::quick_stop_active);
  CHECK("CIA-state-0f",
        decode_statusword(0x000f).state == Cia402State::fault_reaction_active);
  CHECK("CIA-state-08", decode_statusword(0x0008).state == Cia402State::fault);

  const auto unknown = decode_statusword(0x0001);
  CHECK("CIA-004", unknown.state == Cia402State::unknown);
  CHECK("CIA-004", unknown.raw_statusword == 0x0001);

  const auto dual = decode_dual_axis_status(0x00270040U);
  CHECK("CIA-005", dual.low_half.state == Cia402State::switch_on_disabled);
  CHECK("CIA-005", dual.high_half.state == Cia402State::operation_enabled);
  CHECK("CIA-005", !dual.physical_axis_mapping_known);

  const auto enabled = decode_dual_axis_status(0x00270027U);
  CHECK("CIA-003", !velocity_motion_eligible(enabled, 2));
  CHECK("CIA-003-mode3", velocity_motion_eligible(enabled, 3));

  TransitionTracker tracker;
  const MonotonicTime t0{};
  tracker.start(Cia402State::ready_to_switch_on, 10, t0, 500ms);
  CHECK("CIA-002-replay",
        tracker.evaluate(Cia402State::ready_to_switch_on, 10, t0 + 1ms) ==
            TransitionResult::observation_replayed);
  CHECK("CIA-002", tracker.evaluate(Cia402State::switch_on_disabled, 11,
                                    t0 + 500ms) == TransitionResult::timed_out);
}

} // namespace

/**
 * Execute Phase 2 domain behavior-lock tests without an external framework.
 *
 * @return EXIT_SUCCESS when every invariant passes, otherwise EXIT_FAILURE.
 */
int main() {
  test_time_and_sample_contract();
  test_arbitration();
  test_safety();
  test_cia402();
  if (failures != 0) {
    std::cerr << "domain_behavior_lock failures=" << failures << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "domain_behavior_lock passed\n";
  return EXIT_SUCCESS;
}
