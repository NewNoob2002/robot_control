#pragma once

#include "domain/time/monotonic_time.hpp"

#include <cstdint>

namespace robot_control::domain::command {

enum class Source : std::uint8_t {
  none = 0,
  sbus,
  external,
};

struct MotionCommand {
  std::int32_t left_rpm{0};
  std::int32_t right_rpm{0};
  bool stop_requested{true};

  /**
   * Return whether this command requests no motion.
   *
   * @return True for explicit stop or two zero targets.
   *
   * Thread safety: Pure and reentrant.
   */
  [[nodiscard]] constexpr bool is_zero() const noexcept {
    return stop_requested || (left_rpm == 0 && right_rpm == 0);
  }
};

struct CommandSample {
  Source source{Source::none};
  MotionCommand command{};
  time::MonotonicTime captured_at{};
  std::uint64_t session_generation{0};
  std::uint64_t authorization_generation{0};
  std::uint64_t sequence{0};
  bool valid{false};
  bool coherent{false};
  bool enabled{false};
  bool lost{false};
  bool failsafe{false};
};

/**
 * Validate source identity and non-temporal sample invariants.
 *
 * @param sample Producer sample to validate.
 * @param expected Expected producer identity.
 * @return True only for a coherent, enabled, healthy sample with nonzero
 * session, authorization, and sequence identifiers.
 *
 * Thread safety: Pure and reentrant.
 */
[[nodiscard]] constexpr bool
structurally_valid(const CommandSample &sample,
                   const Source expected) noexcept {
  return sample.source == expected && sample.valid && sample.coherent &&
         sample.enabled && !sample.lost && !sample.failsafe &&
         sample.session_generation != 0 &&
         sample.authorization_generation != 0 && sample.sequence != 0;
}

} // namespace robot_control::domain::command
