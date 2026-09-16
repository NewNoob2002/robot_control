#pragma once

#include "domain/drive/runtime.hpp"

#include <array>

namespace robot_control::application::control {
namespace command = domain::command;
namespace drive = domain::drive;
namespace time = domain::time;
namespace safety = domain::safety;

/** Startup-only policy; physical bindings deliberately have no valid defaults. */
struct CycleConfig {
    domain::control::ArbiterConfig arbiter{};
    drive::RuntimeConfig runtime{};
    time::Duration period{std::chrono::milliseconds{10}};
    time::Duration transition_timeout{std::chrono::milliseconds{500}};
};

/** Owned producer copies; emergency stop must be explicitly known on every tick. */
struct CycleInput {
    command::CommandSample sbus{};
    command::CommandSample external{};
    bool coherent{false};
    bool emergency_stop_known{false};
    bool emergency_stop_active{false};
    bool shutdown_requested{false};
};

/** Final offline publication plus the exact safety envelope and selected intent. */
struct CycleResult {
    domain::control::SelectedCommand selected{};
    drive::RuntimeRequest request{};
    drive::RuntimeOutput output{};
};

/** Validate all startup limits without resources; pure and reentrant. */
[[nodiscard]] bool valid_cycle_config(const CycleConfig& config) noexcept;

/**
 * Single control owner, no I/O, worker, wall clock, reset or automatic rearm.
 * The sole RuntimeSession observes every drive event. Prepare/submit/complete
 * run synchronously on its owner, nominally every 10 ms.
 * Producers provide owned coherent snapshots; only RuntimeSession publishes bytes.
 * This offline owner does not prove layout readbacks or physical send completion.
 * Instances are noncopyable; every method must run on the same owner thread.
 */
class ControlCycle final {
  public:
    /** Copy startup configuration; invalid timing/binding permanently inhibits. */
    explicit ControlCycle(CycleConfig config) noexcept;
    ControlCycle(const ControlCycle&) = delete;
    ControlCycle& operator=(const ControlCycle&) = delete;
    /**
     * Prepare one decision against the sole runtime guard's current snapshot.
     * @param input Immutable producer copies and explicit safety inputs.
     * @param state RuntimeSession snapshot; caller must process events/deadlines first.
     * @param now Injected monotonic time. Regressions permanently inhibit.
     * @return Owned selection/request; output is populated only by complete().
     * Thread safety: Same owner as RuntimeSession. Every prepare requires exactly
     * one submit/evaluate followed by complete before the next prepare.
     */
    [[nodiscard]] CycleResult prepare(const CycleInput& input, const drive::RuntimeState& state,
                                      time::MonotonicTime now) noexcept;
    /**
     * Acknowledge the actual runtime outcome, including rejection/send failure.
     * @param result Matching prepared result, updated with the actual output.
     * @param state Same runtime guard after the submission, never a second guard.
     * @param now Monotonic completion time. Borrowed arguments are not retained.
     * Thread safety: Single owner; mismatched/out-of-order completion inhibits.
     */
    void complete(CycleResult& result, const drive::RuntimeState& state, time::MonotonicTime now) noexcept;

  private:
    /** Source authorization identity; session alone does not grant motion. */
    struct Authority {
        command::Source source{command::Source::none};
        std::uint64_t session{0};
        std::uint64_t generation{0};
        /** Compare the complete source/rearm identity. */
        bool operator==(const Authority&) const = default;
    };
    /** Retire one identity without allowing older sessions to lower the floor. */
    void consume(Authority authority) noexcept;
    /** Revoke current/pending ownership and retire its source authorization. */
    void revoke() noexcept;
    /** Check an identity against the last consumed source/session generation. */
    [[nodiscard]] bool fresh(Authority authority) const noexcept;
    /** Return the stable array index for a known producer. */
    [[nodiscard]] static std::size_t index(command::Source source) noexcept;

    const CycleConfig config_;
    domain::control::ControlArbiter arbiter_;
    safety::SafetyManager safety_{};
    std::uint64_t awaiting_decision_{0};
    std::array<Authority, 2> consumed_{};
    Authority active_{};
    Authority pending_{};
    Authority sbus_interlock_{};
    time::MonotonicTime zero_since_{};
    time::MonotonicTime transition_since_{};
    time::MonotonicTime last_tick_{};
    time::MonotonicTime transition_status_at_{};
    drive::Cia402State expected_{drive::Cia402State::unknown};
    std::uint64_t authorization_{0};
    std::uint64_t runtime_epoch_{0};
    bool have_tick_{false};
    bool stopped_{false};
};
} // namespace robot_control::application::control
