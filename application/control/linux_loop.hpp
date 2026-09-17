#pragma once

#include "application/control/control_cycle.hpp"
#include "communication/canopen/runtime.hpp"
#include "input/sbus/linux/source_bridge.hpp"

namespace robot_control::application::control {

/** Owned diagnostics: the primary failure and cleanup result are both retained. */
struct LoopResult {
    platform::linux::Status status{};
    platform::linux::Status stop_status{};
    communication::canopen::LifecycleExit exit{communication::canopen::LifecycleExit::deadline};
    input::sbus::SourceSnapshot source{};
    CycleResult control{};
    time::Duration maximum_lateness{};
    time::Duration maximum_cycle_time{};
    time::Duration shutdown_elapsed{};
    std::uint64_t runtime_session{0};
    std::uint64_t cycles{0};
    std::uint64_t missed_periods{0};
    bool finished{false};
};

/**
 * Debug-only bounded application owner. One RuntimeSession owns the only guard,
 * socket output and per-event inhibition. This class owns no thread or endpoint.
 * Borrowed Lifecycle, Reader and Source must outlive it and run on this same owner.
 * Call stop and inspect both statuses before destruction; destruction sends nothing.
 */
class ControlLoop final {
  public:
    using CreateResult = platform::linux::Result<std::unique_ptr<ControlLoop>>;
    /**
     * Attach one runtime session after caller-verified current layout readbacks.
     * @param owner Borrowed CANopen owner, already opened on an existing interface.
     * @param reader Borrowed open SBUS reader; no open/reconnect is performed here.
     * @param source Borrowed validated producer, reset to require a new reader session.
     * @param config Copied cycle and physical binding parameters.
     * @param proof Current layout readbacks; virtual tests must identify them as fixtures.
     * @param shutdown_timeout Positive reporting deadline, at most one second.
     * @param right_throttle_only Debug qualification: retain raw Source, require neutral steering
     * and nonnegative throttle for valid enabled samples, suppress left command before arbitration.
     * Invalid/disabled input retains normal zero-output policy; default is unchanged.
     * @return Owned loop or contextual failure. No CAN send occurs during creation.
     * Thread safety: Single owner; all borrowed objects must outlive the result.
     */
    [[nodiscard]] static CreateResult create(communication::canopen::Lifecycle& owner, input::sbus::Reader& reader,
                                             input::sbus::Source& source, CycleConfig config,
                                             const communication::canopen::RuntimeLayoutProof& proof,
                                             time::Duration shutdown_timeout = std::chrono::milliseconds{100},
                                             bool right_throttle_only = false);
    ControlLoop(const ControlLoop&) = delete;
    ControlLoop& operator=(const ControlLoop&) = delete;
    /** Detach the session without implicit I/O; explicit stop is required. */
    ~ControlLoop() = default;
    /**
     * Run CAN events until the next cycle, read one bounded UART batch, then submit.
     * @param input External command and safety inputs; sbus is always replaced by Source.
     * @param captured_batch Optional caller-owned diagnostic copy of the exact read batch.
     * Cleared on entry, filled only after a successful read, never retained or used for policy.
     * @return Owned diagnostic/result snapshot. Errors/signals finish the loop permanently.
     * Thread safety: Single owner, no callbacks. Late cycles skip catch-up publications.
     */
    [[nodiscard]] LoopResult step(CycleInput input, input::sbus::ReadBatch* captured_batch = nullptr);
    /** Inhibit intake, stop Source and attempt one safe RPDO within the configured deadline. */
    [[nodiscard]] LoopResult stop();
    /** Return actual guard diagnostics without refreshing timestamps; owner only. */
    [[nodiscard]] drive::RuntimeState runtime_state() const noexcept;

  private:
    /** Bind already validated resources without I/O; caller owns borrowed lifetimes. */
    ControlLoop(communication::canopen::Lifecycle& owner, input::sbus::Reader& reader, input::sbus::Source& source,
                CycleConfig config, std::unique_ptr<communication::canopen::RuntimeSession> runtime,
                time::Duration shutdown_timeout, bool right_throttle_only);
    /** Preserve the first failure, stop once and latch signal/deadline disposition. */
    [[nodiscard]] LoopResult finish(platform::linux::Status status, communication::canopen::LifecycleExit exit,
                                    time::MonotonicTime started);

    communication::canopen::Lifecycle& owner_;
    input::sbus::Reader& reader_;
    input::sbus::Source& source_;
    const CycleConfig config_;
    const time::Duration shutdown_timeout_;
    const bool right_throttle_only_;
    std::unique_ptr<communication::canopen::RuntimeSession> runtime_;
    ControlCycle cycle_;
    time::MonotonicTime next_cycle_{};
    LoopResult result_{};
};
} // namespace robot_control::application::control
