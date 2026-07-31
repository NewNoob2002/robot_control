#pragma once

#include "domain/time/monotonic_time.hpp"

#include <cstdint>

namespace robot_control::domain::drive {

enum class Cia402State : std::uint8_t {
  unknown = 0,
  not_ready_to_switch_on,
  switch_on_disabled,
  ready_to_switch_on,
  switched_on,
  operation_enabled,
  quick_stop_active,
  fault_reaction_active,
  fault,
};

struct AxisStatus {
  Cia402State state{Cia402State::unknown};
  std::uint16_t raw_statusword{0};
  std::uint16_t non_state_bits{0};
};

struct DualAxisStatus {
  AxisStatus low_half{};
  AxisStatus high_half{};
  std::uint32_t raw_status{0};
  bool physical_axis_mapping_known{false};
};

/**
 * Decode one raw CiA402 statusword with the standard 0x006f state mask.
 *
 * @param raw Raw 16-bit statusword.
 * @return Decoded state and retained raw/non-state bits.
 *
 * Thread safety: Pure and reentrant.
 */
[[nodiscard]] AxisStatus decode_statusword(std::uint16_t raw) noexcept;

/**
 * Decode neutral low/high halves without assigning physical axis names.
 *
 * @param raw Raw vendor 32-bit status value.
 * @return Two independently decoded halves with raw value preserved.
 *
 * Thread safety: Pure and reentrant.
 */
[[nodiscard]] DualAxisStatus
decode_dual_axis_status(std::uint32_t raw) noexcept;

/**
 * Determine whether a decoded dual-axis observation permits velocity motion.
 *
 * @param status Independently decoded status halves.
 * @param actual_mode Value observed from object 0x6061.
 * @param requested_mode Requested operation mode, normally velocity mode 3.
 * @return True only when both halves are operation enabled and the mode
 * display exactly matches the request.
 *
 * Thread safety: Pure and reentrant.
 */
[[nodiscard]] bool
velocity_motion_eligible(const DualAxisStatus &status, std::int8_t actual_mode,
                         std::int8_t requested_mode = 3) noexcept;

enum class TransitionResult : std::uint8_t {
  idle = 0,
  pending,
  reached,
  timed_out,
  observation_replayed,
};

class TransitionTracker final {
public:
  /**
   * Start verification of one expected state using a newer observation.
   *
   * @param expected Expected state.
   * @param observation_generation Latest generation at command emission.
   * @param now Current monotonic time.
   * @param timeout Positive transition timeout.
   *
   * Thread safety: Single-owner only.
   */
  void start(Cia402State expected, std::uint64_t observation_generation,
             time::MonotonicTime now, time::Duration timeout) noexcept;

  /**
   * Evaluate transition progress using a status observation.
   *
   * @param actual Actual decoded state.
   * @param observation_generation Status observation generation.
   * @param now Current monotonic time.
   * @return Pending, reached, timeout, replay, or idle.
   *
   * Thread safety: Single-owner only.
   */
  [[nodiscard]] TransitionResult evaluate(Cia402State actual,
                                          std::uint64_t observation_generation,
                                          time::MonotonicTime now) noexcept;

private:
  Cia402State expected_{Cia402State::unknown};
  std::uint64_t command_observation_generation_{0};
  time::MonotonicTime deadline_{};
  bool pending_{false};
};

} // namespace robot_control::domain::drive
