#pragma once

#include "domain/command/command_sample.hpp"

#include <chrono>
#include <cstdint>

namespace robot_control::domain::control {

enum class ArbiterReason : std::uint32_t {
  none = 0,
  input_incoherent = 1U << 0U,
  no_valid_source = 1U << 1U,
  sbus_unavailable = 1U << 2U,
  external_unavailable = 1U << 3U,
  zero_qualifying = 1U << 4U,
  rearm_required = 1U << 5U,
  rearm_replay = 1U << 6U,
  sequence_replay = 1U << 7U,
};

struct ArbiterConfig {
  time::Duration sbus_timeout{std::chrono::milliseconds{100}};
  time::Duration external_timeout{std::chrono::milliseconds{100}};
  time::Duration handover_zero_dwell{std::chrono::milliseconds{150}};
  std::int32_t max_abs_rpm{1000};
};

struct ArbiterInput {
  command::CommandSample sbus{};
  command::CommandSample external{};
  bool coherent{false};
};

struct SelectedCommand {
  command::Source source{command::Source::none};
  command::MotionCommand command{};
  time::MonotonicTime selected_at{};
  std::uint64_t source_session_generation{0};
  std::uint64_t authorization_generation{0};
  std::uint64_t handover_generation{0};
  std::uint32_t reason_flags{0};
  bool valid{false};

  /**
   * Return whether the selected result is fail-closed.
   *
   * @return True when no motion-producing source is selected.
   *
   * Thread safety: Pure and reentrant.
   */
  [[nodiscard]] constexpr bool inhibited() const noexcept {
    return !valid || source == command::Source::none || command.is_zero();
  }
};

class ControlArbiter final {
public:
  /**
   * Construct an inhibited arbiter with explicit timing and command limits.
   *
   * @param config Immutable policy configuration copied by value.
   *
   * Thread safety: Instances are single-owner and not internally synchronized.
   */
  explicit ControlArbiter(ArbiterConfig config) noexcept;

  /**
   * Select one command from a coherent pair of producer snapshots.
   *
   * @param input Immutable producer snapshots for one control cycle.
   * @param now Current injected monotonic time.
   * @return One selected command; unsafe, stale, or replayed input returns a
   * zero, invalid selection.
   *
   * Thread safety: Single-owner only.
   */
  [[nodiscard]] SelectedCommand evaluate(const ArbiterInput &input,
                                         time::MonotonicTime now) noexcept;

  /**
   * Return the current handover generation.
   *
   * @return Monotonically increasing ownership-loss generation.
   *
   * Thread safety: Single-owner only.
   */
  [[nodiscard]] std::uint64_t handover_generation() const noexcept;

private:
  struct ProducerHistory {
    std::uint64_t session_generation{0};
    std::uint64_t sequence{0};
  };

  /** Validate freshness, structure, session, and sequence for one producer. */
  [[nodiscard]] bool
  sample_usable(const command::CommandSample &sample, command::Source expected,
                time::Duration timeout, time::MonotonicTime now,
                ProducerHistory &history, std::uint32_t &reasons) noexcept;
  /** Update the continuous both-sources-zero qualification interval. */
  void update_zero_tracking(const ArbiterInput &input, bool sbus_usable,
                            bool external_usable,
                            time::MonotonicTime now) noexcept;
  /** Construct a zero, source-none selection with diagnostic reasons. */
  [[nodiscard]] SelectedCommand inhibited(time::MonotonicTime now,
                                          std::uint32_t reasons) const noexcept;
  /** Clamp a requested command to the configured absolute RPM limit. */
  [[nodiscard]] command::MotionCommand
  bounded(const command::MotionCommand &command) const noexcept;

  ArbiterConfig config_;
  ProducerHistory sbus_history_{};
  ProducerHistory external_history_{};
  time::MonotonicTime both_zero_since_{};
  std::uint64_t last_external_authorization_generation_{0};
  std::uint64_t required_external_authorization_generation_{0};
  std::uint64_t handover_generation_{0};
  bool both_zero_tracking_{false};
  bool external_recovery_required_{true};
  bool manual_was_authoritative_{false};
};

} // namespace robot_control::domain::control
