#pragma once

#include "domain/drive/zlac8015d.hpp"
#include "domain/safety/safety_manager.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace robot_control::domain::drive {

/** Explicit half selection; unknown is deliberately not a deployment default. */
enum class PackedHalf : std::uint8_t { unknown, low, high };

/** Injected physical binding and strict runtime deadlines. */
struct RuntimeConfig {
    PackedHalf left_target{PackedHalf::unknown};
    PackedHalf left_feedback{PackedHalf::unknown};
    int left_sign{0};
    int right_sign{0};
    std::int32_t max_abs_rpm{0};
    std::int32_t standstill_tenths_rpm{0};
    time::Duration heartbeat_timeout{};
    time::Duration feedback_timeout{};
    time::Duration decision_timeout{};
    std::uint32_t emergency_status_mask{0}; // Deployment-confirmed active-high bits; zero leaves binding external.
};

/** Identify one transport lifetime and remote boot, including online boot zero. */
struct RuntimeGeneration {
    std::uint64_t transport{0};
    std::uint64_t boot{0};
    /** Compare complete generation identity without assigning physical axes. */
    [[nodiscard]] bool operator==(const RuntimeGeneration&) const = default;
};

/** Protocol-independent, immutable input observation with original raw values. */
struct RuntimeFeedback {
    RuntimeGeneration generation{};
    std::uint64_t version{0};
    time::MonotonicTime heartbeat_at{};
    time::MonotonicTime status_at{};
    time::MonotonicTime diagnostics_at{};
    std::uint32_t status_raw{0};
    std::uint32_t velocity_raw{0};
    std::uint32_t fault_raw{0};
    std::int8_t mode_raw{0};
    bool current{false};
    bool operational{false};
};

/** P10 owner envelope: authorization remains owned by the robot safety policy. */
struct RuntimeRequest {
    safety::SafetyDecision decision{};
    RuntimeGeneration generation{};
    std::uint64_t feedback_epoch{0};
    std::uint64_t authorization{0};
    time::MonotonicTime issued_at{};
};

/** Why both targets are inhibited; raw feedback is retained separately. */
enum class RuntimeReason : std::uint8_t {
    startup,
    none,
    config_invalid,
    feedback_invalid,
    clock_invalid,
    generation_changed,
    decision_invalid,
    decision_expired,
    rearm_required,
    zero_required,
    transition_invalid,
    transport_error,
    shutdown,
    exhausted
};

/** One exact 6040/60ff:03 RPDO payload, defaulting to Shutdown plus two zeros. */
struct RuntimeOutput {
    std::array<std::byte, 6> payload{std::byte{6}};
    command::MotionCommand command{};
    RuntimeReason reason{RuntimeReason::startup};
    bool accepted{false};
};

/** Value snapshot; access is restricted to the sole runtime/control owner. */
struct RuntimeState {
    RuntimeFeedback feedback{};
    RuntimeOutput output{};
    std::uint64_t epoch{1};
    std::uint64_t authorization{0};
    std::int32_t left_tenths_rpm{0};
    std::int32_t right_tenths_rpm{0};
    bool healthy{false};
    bool armed{false};
    bool stopped{false};
};

/** Validate an explicit binding; pure/reentrant, retains no caller resources. */
[[nodiscard]] bool valid_runtime_config(const RuntimeConfig& config) noexcept;

/**
 * Pure, single-owner runtime guard. No clocks, threads, sockets or resets.
 * Call observe on every ordered feedback event and periodically without input.
 * A failed physical send must immediately call inhibit(transport_error).
 */
class RuntimePolicy final {
  public:
    /** Copy configuration; invalid configuration remains permanently inhibited. */
    explicit RuntimePolicy(RuntimeConfig config) noexcept;
    /** Consume a value observation at injected monotonic now; single-owner only. */
    void observe(const RuntimeFeedback& feedback, time::MonotonicTime now) noexcept;
    /** Evaluate a fresh owner decision; returns owned bytes, never a cached target. */
    [[nodiscard]] RuntimeOutput evaluate(const RuntimeRequest& request, time::MonotonicTime now) noexcept;
    /** Revoke the active authorization and both targets; caller owns bus cleanup. */
    void inhibit(RuntimeReason reason) noexcept;
    /** Permanently inhibit this policy instance; no automatic restart is allowed. */
    void stop(RuntimeReason reason = RuntimeReason::shutdown) noexcept;
    /** Copy diagnostics without refreshing their timestamps; single-owner only. */
    [[nodiscard]] RuntimeState state() const noexcept;

  private:
    /** Validate monotonic progression without allowing signed duration overflow. */
    [[nodiscard]] bool advance(time::MonotonicTime now) noexcept;
    /** Encode a reviewed zero transition or a bounded, signed pair of targets. */
    [[nodiscard]] RuntimeOutput publish(zlac8015d::TransitionControlword word, command::MotionCommand command,
                                        RuntimeReason reason) noexcept;
    RuntimeConfig config_;
    RuntimeState state_{};
    time::MonotonicTime last_now_{};
    time::MonotonicTime last_request_at_{};
    time::MonotonicTime zero_at_{};
    std::uint64_t last_decision_{0};
    std::uint64_t highest_authorization_{0};
    bool zero_confirmed_{false};
    bool enabled_seen_{false};
};

} // namespace robot_control::domain::drive
