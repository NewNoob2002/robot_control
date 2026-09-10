#pragma once

#include "communication/canopen/lifecycle.hpp"
#include "domain/drive/zlac8015d.hpp"

#include <chrono>
#include <cstdint>
#include <span>

namespace robot_control::communication::canopen {

/** Node-1 NMT commands available only to the Phase 6 qualification artifact. */
enum class QualificationNmt : std::uint8_t {
    operational = 0x01U,
    stopped = 0x02U,
    pre_operational = 0x80U,
};

/** Fail-closed state of one qualification-process lifetime. */
enum class QualificationState : std::uint8_t { ready, cleanup_required };

/** Separately authorized physical communication stimuli; never combined in one trial. */
enum class CommunicationLossStimulus : std::uint8_t { watchdog, heartbeat, tpdo };

/** Execute exact volatile ZLAC operations through the sole CANopen owner. */
class QualificationSession final {
  public:
    /**
     * Borrow one active qualification lifecycle owner.
     *
     * @param lifecycle Owner retained by reference for this session lifetime.
     *
     * Thread safety: Construct and use only on the lifecycle owner thread.
     */
    explicit QualificationSession(Lifecycle& lifecycle) noexcept;

    /**
     * Return the current fail-closed session state.
     *
     * @return Ready until any operation fails, then cleanup-required forever.
     *
     * Thread safety: Qualification owner thread only.
     */
    [[nodiscard]] QualificationState state() const noexcept;

    /**
     * Send one exact node-1 qualification NMT command.
     *
     * @param command Start, Stopped, or Pre-operational.
     * @return Success after one kernel submission; failure inhibits the session.
     *
     * Thread safety: Qualification owner thread only.
     */
    [[nodiscard]] platform::linux::Status send_nmt(QualificationNmt command) noexcept;

    /**
     * Set velocity mode and require the exact mode-display readback.
     *
     * @return Success only after 0x6061:00 reads back 3.
     *
     * Thread safety: Qualification owner thread only.
     */
    [[nodiscard]] platform::linux::Status set_velocity_mode() noexcept;

    /**
     * Send one reviewed CiA402 transition controlword.
     *
     * @param controlword Typed 0x0000, 0x0006, 0x0007, or 0x000F request.
     * @return Success after one exact SDO download; failure inhibits the session.
     *
     * Thread safety: Qualification owner thread only.
     */
    [[nodiscard]] platform::linux::Status
    send_controlword(domain::drive::zlac8015d::TransitionControlword controlword) noexcept;

    /**
     * Write and verify both independent targets as zero.
     *
     * @return Success after exact download/readback pairs for subindices 1 and 2.
     *
     * Thread safety: Qualification owner thread only. Safe to call for cleanup.
     */
    [[nodiscard]] platform::linux::Status set_zero_targets() noexcept;

    /**
     * Temporarily map packed speed before status and observe manual rotation without motor commands.
     * @param duration Capture window in (0, 60s]; the CLI fixes this to 60s.
     * @return Success for capture/verified mapping restoration, not physical speed qualification.
     * Thread safety: Owner thread only; consumes the session. Borrows the owner's lifetime.
     */
    [[nodiscard]] platform::linux::Status capture_manual_tpdo(std::chrono::milliseconds duration) noexcept;

    /**
     * Execute and clean up one zero-target CiA402 qualification sequence.
     *
     * @param transition_timeout Positive per-state deadline no greater than five seconds.
     * @return Success only after Operational, mode/zero verification, three newer dual-state observations, and
     * verified Shutdown plus Pre-operational cleanup.
     *
     * Thread safety: Qualification owner thread only; completion permanently requires cleanup/restart.
     */
    [[nodiscard]] platform::linux::Status
    qualify_zero_target_cia402(std::chrono::milliseconds transition_timeout) noexcept;

    /**
     * Execute one non-renewable, start-anchored target interval.
     *
     * @param channel Neutral independent subindex to command.
     * @param rpm Nonzero whole-rpm target with absolute value at most 10.
     * @param duration Positive duration no greater than three seconds.
     * @return Success only after both-zero preflight, bounded target, expiry, and verified zero.
     *
     * Thread safety: Qualification owner thread only; callable once per session.
     */
    [[nodiscard]] platform::linux::Status run_target_once(domain::drive::zlac8015d::IndependentChannel channel,
                                                          std::int32_t rpm,
                                                          std::chrono::milliseconds duration) noexcept;

    /**
     * Enter zero-target Operation Enabled, execute one target interval, and restore the fixture.
     *
     * @param channel Neutral independent subindex to command.
     * @param rpm Nonzero whole-rpm target with absolute value at most 10.
     * @param duration Positive target interval no greater than three seconds.
     * @param transition_timeout Positive per-state deadline no greater than five seconds.
     * @return Success only after volatile asynchronous selection, verified motion cleanup, and baseline restoration.
     *
     * Thread safety: Qualification owner thread only; callable once per session.
     */
    [[nodiscard]] platform::linux::Status
    qualify_first_motion_cia402(domain::drive::zlac8015d::IndependentChannel channel, std::int32_t rpm,
                                std::chrono::milliseconds duration,
                                std::chrono::milliseconds transition_timeout) noexcept;

    /**
     * Enter bounded motion, issue NMT Stopped, zero in Pre-operational, and restore the fixture.
     *
     * @param channel Neutral independent subindex to command.
     * @param rpm Nonzero whole-rpm target with absolute value at most 10.
     * @param duration Positive pre-stop interval no greater than ten seconds.
     * @param transition_timeout Positive per-state deadline no greater than five seconds.
     * @return Success only after observed Stopped, verified zero, Operational cleanup, and baseline restoration.
     *
     * Thread safety: Qualification owner thread only; callable once per session.
     */
    [[nodiscard]] platform::linux::Status
    qualify_nmt_stop_cia402(domain::drive::zlac8015d::IndependentChannel channel, std::int32_t rpm,
                            std::chrono::milliseconds duration, std::chrono::milliseconds transition_timeout) noexcept;

    /**
     * Enter bounded motion, issue CiA402 Shutdown once, verify zero velocity, and restore the fixture.
     *
     * @param channel Neutral independent subindex to command.
     * @param rpm Nonzero whole-rpm target with absolute value at most 10.
     * @param duration Positive pre-stop interval no greater than ten seconds.
     * @param transition_timeout Positive per-state deadline no greater than five seconds.
     * @return Success only after observed Ready to Switch On, verified zero velocity/targets, and baseline restoration.
     *
     * Thread safety: Qualification owner thread only; callable once per session.
     */
    [[nodiscard]] platform::linux::Status
    qualify_shutdown_cia402(domain::drive::zlac8015d::IndependentChannel channel, std::int32_t rpm,
                            std::chrono::milliseconds duration, std::chrono::milliseconds transition_timeout) noexcept;

    /**
     * Enter bounded motion, issue CiA402 Disable Voltage once, verify zero velocity, and restore volatile values.
     *
     * @param channel Neutral independent subindex to command.
     * @param rpm Nonzero whole-rpm target with absolute value at most 10.
     * @param duration Positive pre-stop interval no greater than ten seconds.
     * @param transition_timeout Positive per-state deadline no greater than five seconds.
     * @return Success only after observed Switch On Disabled, verified zero velocity/targets, and baseline restoration.
     *
     * Thread safety: Qualification owner thread only; callable once per session.
     */
    [[nodiscard]] platform::linux::Status
    qualify_disable_voltage_cia402(domain::drive::zlac8015d::IndependentChannel channel, std::int32_t rpm,
                                   std::chrono::milliseconds duration,
                                   std::chrono::milliseconds transition_timeout) noexcept;

    /**
     * Enter bounded motion, require quick-stop option 5, issue Quick Stop once, and restore volatile values.
     *
     * @param channel Neutral independent subindex to command.
     * @param rpm Nonzero whole-rpm target with absolute value at most 10.
     * @param duration Positive pre-stop interval no greater than ten seconds.
     * @param transition_timeout Positive per-state deadline no greater than five seconds.
     * @return Success only after observed Quick Stop Active, verified zero velocity/targets, and baseline restoration.
     *
     * Thread safety: Qualification owner thread only; callable once per session.
     */
    [[nodiscard]] platform::linux::Status
    qualify_quick_stop_cia402(domain::drive::zlac8015d::IndependentChannel channel, std::int32_t rpm,
                              std::chrono::milliseconds duration,
                              std::chrono::milliseconds transition_timeout) noexcept;

    /**
     * Attempt the fixed usable-CAN cleanup sequence without rearming motion.
     *
     * @return First cleanup failure, or success after zero, Shutdown, and Pre-operational submissions.
     *
     * Thread safety: Qualification owner thread only. The state remains cleanup-required.
     */
    [[nodiscard]] platform::linux::Status cleanup() noexcept;

    /**
     * Qualify one fixed right-axis +5 rpm communication-loss trial with a 1000 ms watchdog.
     * @param stimulus Exactly one watchdog silence, heartbeat suppression, or TPDO event suppression.
     * @return Success only after the selected observation and verified safe baseline restoration.
     * Thread safety: Owner thread only; borrows the existing lifecycle, once per session.
     * Requires heartbeat/TPDO deadlines of 500/100 ms; physical attribution also requires independent capture.
     */
    [[nodiscard]] platform::linux::Status
    qualify_communication_loss_cia402(CommunicationLossStimulus stimulus) noexcept;

  private:
    /** Return success only while ready with a current node-1 heartbeat. */
    [[nodiscard]] platform::linux::Status require_ready() const noexcept;

    /** Return success only for fixed node 1 with a current heartbeat. */
    [[nodiscard]] platform::linux::Status require_fresh_remote() const noexcept;

    /** Return success while the selected transport generation remains clean. */
    [[nodiscard]] platform::linux::Status require_clean_generation() const noexcept;

    /** Require fresh motion feedback with no current EMCY or CAN error. */
    [[nodiscard]] platform::linux::Status require_motion_feedback() const noexcept;

    /** Require dual Operation Enabled and zero velocity on the uncommanded protocol half. */
    [[nodiscard]] platform::linux::Status
    require_operation_enabled_feedback(domain::drive::zlac8015d::IndependentChannel channel) const noexcept;

    /** Require exact zero independent and packed velocity readbacks before the supplied deadline. */
    [[nodiscard]] platform::linux::Status require_zero_velocity_feedback(
        bool require_fresh,
        std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max()) noexcept;

    /** Wait for a newer matching node-1 NMT heartbeat. */
    [[nodiscard]] platform::linux::Status wait_nmt_state(RemoteNmtState expected,
                                                         std::chrono::steady_clock::time_point previous,
                                                         std::chrono::milliseconds timeout) noexcept;

    /** Wait for one fresh heartbeat after enabling the temporary producer. */
    [[nodiscard]] platform::linux::Status wait_heartbeat(std::chrono::steady_clock::time_point previous,
                                                         std::chrono::milliseconds timeout) noexcept;

    /** Wait for one newer TPDO1 whose two status halves match and whose packed velocity is zero. */
    [[nodiscard]] platform::linux::Status wait_dual_state(domain::drive::Cia402State expected,
                                                          std::chrono::steady_clock::time_point previous,
                                                          std::chrono::milliseconds timeout) noexcept;

    /** Restore verified zero, Shutdown, and Pre-operational after a successful zero-target sequence. */
    [[nodiscard]] platform::linux::Status
    cleanup_zero_target_cia402(std::chrono::milliseconds transition_timeout) noexcept;

    /** Run the shared read-only zero-target preflight. */
    [[nodiscard]] platform::linux::Status preflight_zero_target_cia402() noexcept;

    /** Enter Operation Enabled with both targets and all velocity feedback zero. */
    [[nodiscard]] platform::linux::Status
    enter_zero_target_operation_enabled(std::chrono::milliseconds transition_timeout) noexcept;

    /** Write and verify one volatile 0x200F command-application value. */
    [[nodiscard]] platform::linux::Status set_command_application_raw(std::uint16_t value, bool require_fresh) noexcept;

    /** Write and verify the temporary 0x1017 heartbeat-producer interval. */
    [[nodiscard]] platform::linux::Status set_heartbeat_producer_raw(std::uint16_t milliseconds,
                                                                     bool require_fresh) noexcept;

    /** Restore the disabled heartbeat-producer baseline when required. */
    [[nodiscard]] platform::linux::Status restore_heartbeat_producer() noexcept;

    /** Execute one already-authorized target interval and verify the selected target returns to zero. */
    [[nodiscard]] platform::linux::Status run_target_interval(domain::drive::zlac8015d::IndependentChannel channel,
                                                              std::int32_t rpm,
                                                              std::chrono::milliseconds duration) noexcept;

    /** Hold one verified nonzero target until the bounded stop deadline. */
    [[nodiscard]] platform::linux::Status run_motion_interval(domain::drive::zlac8015d::IndependentChannel channel,
                                                              std::int32_t rpm,
                                                              std::chrono::milliseconds duration) noexcept;

    /** Issue NMT Stopped after one bounded target interval, then restore Operational with verified zero targets. */
    [[nodiscard]] platform::linux::Status run_nmt_stop_interval(domain::drive::zlac8015d::IndependentChannel channel,
                                                                std::int32_t rpm, std::chrono::milliseconds duration,
                                                                std::chrono::milliseconds transition_timeout) noexcept;

    /** Issue one terminal controlword after bounded motion and verify the expected state and zero velocity. */
    [[nodiscard]] platform::linux::Status
    run_controlword_stop_interval(domain::drive::zlac8015d::IndependentChannel channel, std::int32_t rpm,
                                  std::chrono::milliseconds duration, std::chrono::milliseconds transition_timeout,
                                  domain::drive::zlac8015d::TransitionControlword controlword,
                                  domain::drive::Cia402State expected_state) noexcept;

    /** Select the exact bounded stop stimulus without combining independent authorizations. */
    enum class StopStimulus : std::uint8_t {
        nmt_stopped,
        shutdown,
        disable_voltage,
        quick_stop,
        watchdog,
        heartbeat_loss,
        tpdo_loss
    };

    /** Write and verify one gate-approved U16 communication setting, without freshness prerequisites. */
    [[nodiscard]] platform::linux::Status set_communication_setting(std::uint16_t index, std::uint8_t subindex,
                                                                    std::uint16_t value) noexcept;

    /** Observe a bounded quiet window or exactly one stale stream; never renew the target. */
    [[nodiscard]] platform::linux::Status observe_communication_loss(StopStimulus stimulus) noexcept;

    /** Require nonzero selected-axis and zero other-axis independent velocity within one 100 ms deadline. */
    [[nodiscard]] platform::linux::Status
    require_independent_motion_feedback(domain::drive::zlac8015d::IndependentChannel channel) noexcept;

    /** Poll independent and packed zero feedback under one deadline; stop on transport/protocol errors. */
    [[nodiscard]] platform::linux::Status wait_zero_velocity(std::chrono::milliseconds timeout) noexcept;

    /** Run the common bounded stop preparation, selected stop stimulus, and baseline restoration. */
    [[nodiscard]] platform::linux::Status qualify_stop_cia402(domain::drive::zlac8015d::IndependentChannel channel,
                                                              std::int32_t rpm, std::chrono::milliseconds duration,
                                                              std::chrono::milliseconds transition_timeout,
                                                              StopStimulus stimulus) noexcept;

    /** Enter Pre-operational, verify both targets zero, and optionally re-enter Operational for normal cleanup. */
    [[nodiscard]] platform::linux::Status restore_zero_after_nmt_stop(std::chrono::milliseconds transition_timeout,
                                                                      bool reenter_operational) noexcept;

    /** Run verified zero-state cleanup and restore the recorded synchronous application mode. */
    [[nodiscard]] platform::linux::Status
    cleanup_first_motion_cia402(std::chrono::milliseconds transition_timeout) noexcept;

    /** Permanently inhibit this session and return the supplied failure. */
    [[nodiscard]] platform::linux::Status inhibit(platform::linux::Status status) noexcept;

    /** Execute one exact expedited SDO download without protocol retry. */
    [[nodiscard]] platform::linux::Status
    download(std::uint16_t index, std::uint8_t subindex, std::span<const std::byte> data, bool require_fresh,
             std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max()) noexcept;

    /** Execute one exact expedited SDO upload without protocol retry. */
    [[nodiscard]] platform::linux::Result<SdoObservation>
    upload(std::uint16_t index, std::uint8_t subindex, bool require_fresh,
           std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max()) noexcept;

    /** Download a value and compare one exact expedited readback. */
    [[nodiscard]] platform::linux::Status verify_download(
        std::uint16_t index, std::uint8_t subindex, std::span<const std::byte> data, std::uint16_t readback_index,
        std::uint8_t readback_subindex, std::span<const std::byte> expected, bool require_fresh,
        std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max()) noexcept;

    /** Write and verify one independent target without changing session state. */
    [[nodiscard]] platform::linux::Status verify_target(
        domain::drive::zlac8015d::IndependentChannel channel, std::int32_t rpm, bool require_fresh,
        std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max()) noexcept;

    /** Submit one NMT command without checking or changing session state. */
    [[nodiscard]] platform::linux::Status send_nmt_raw(QualificationNmt command, bool require_fresh) noexcept;

    /** Submit one controlword without checking or changing session state. */
    [[nodiscard]] platform::linux::Status
    send_controlword_raw(domain::drive::zlac8015d::TransitionControlword controlword, bool require_fresh) noexcept;

    ObservationGeneration sequence_generation_{};
    Lifecycle* lifecycle_{nullptr};
    QualificationState state_{QualificationState::ready};
    bool target_sequence_consumed_{false};
    bool command_application_restore_required_{false};
    bool heartbeat_restore_required_{false};
    bool terminal_controlword_submitted_{false};
    std::uint64_t request_generation_{0};
    std::uint64_t attempt_generation_{0};
};

} // namespace robot_control::communication::canopen
