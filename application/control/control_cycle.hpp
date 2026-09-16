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

/**
 * Single control owner, no I/O, worker, wall clock, reset or automatic rearm.
 * Feed each ordered drive event to observe(), then tick() nominally every 10 ms.
 * Producers provide owned coherent snapshots; only tick publishes final bytes.
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
     * Observe every ordered drive event, retaining transient inhibition.
     * @param feedback Owned-value observation, including raw values and generation.
     * @param now Injected nonnegative monotonic time. No resources are retained.
     * Thread safety: Same owner as tick; no output or physical I/O occurs here.
     */
    void observe(const drive::RuntimeFeedback& feedback, time::MonotonicTime now) noexcept;
    /**
     * Evaluate snapshots once and publish one guarded zero/target result.
     * @param input Borrowed immutable copies; retained data is copied by value.
     * @param now Injected monotonic time; regressions permanently inhibit.
     * @return Owned selection, decision envelope and final encoded output.
     * Thread safety: Single owner only. Caller must not replay output at a later time.
     */
    [[nodiscard]] CycleResult tick(const CycleInput& input, time::MonotonicTime now) noexcept;

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
    drive::RuntimePolicy runtime_;
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
