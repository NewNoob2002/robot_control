#pragma once

#include "application/control/control_cycle.hpp"

#include <cstdint>
#include <optional>

namespace robot_control::application::control {

/** Owner-supplied 128-bit random identity; all-zero values are invalid. */
struct Id128 {
    std::uint64_t high{0};
    std::uint64_t low{0};
    /** Compare complete identities; pure and reentrant. */
    [[nodiscard]] bool operator==(const Id128&) const = default;
    /** Return whether the identity is the invalid all-zero value; pure and reentrant. */
    [[nodiscard]] constexpr bool empty() const noexcept {
        return high == 0 && low == 0;
    }
};

/** Board-issued binding copied to the producer; the deadline and t0 never leave the authority. */
struct ExternalChallenge {
    Id128 app_instance_id{};
    Id128 challenge_id{};
    std::uint64_t connection_generation{0};
    std::uint64_t authorization_generation{0};
    drive::RuntimeGeneration drive_generation{};
    /** Compare the complete binding; pure and reentrant. */
    [[nodiscard]] bool operator==(const ExternalChallenge&) const = default;
};

/** Reply purpose; authorize is an edge event and never counts as neutral qualification. */
enum class ExternalKind : std::uint8_t {
    neutral = 1,
    authorize = 2,
    command = 3,
};

/** Untrusted producer reply, sampled after it received the echoed challenge. */
struct ExternalReply {
    ExternalChallenge challenge{};
    ExternalKind kind{ExternalKind::neutral};
    std::uint64_t sequence{0};
    std::int32_t left_rpm{0};
    std::int32_t right_rpm{0};
    bool stop_requested{true};
};

/** Validation result for one reply; only validated changes authority state beyond consumption. */
enum class ExternalVerdict : std::uint8_t {
    validated,
    inhibited,
    no_challenge,
    sequence_replay,
    challenge_mismatch,
    binding_mismatch,
    expired,
    rpm_range,
    not_neutral,
    not_qualified,
    wrong_phase,
};

/** REVOKED observes only; QUALIFYING tracks a neutral hold; AUTHORIZED forwards fresh candidates. */
enum class ExternalPhase : std::uint8_t {
    revoked,
    qualifying,
    authorized,
};

/**
 * Board-side challenge and authorization issuer for the single configured external producer.
 *
 * It never sends CAN, enables a drive or bypasses ControlArbiter/ControlCycle. While not
 * authorized it provides no external sample, so an observation-only producer cannot be taken
 * as a rearm event. A new authorization generation is issued only by an explicit authorize
 * reply after a continuous neutral hold of `handover_zero_dwell` with SBUS neutral and the
 * drive healthy and stationary. Every forwarded sample uses the board challenge time t0 as
 * its conservative capture time, so forwarding never extends a candidate's lifetime.
 *
 * Random identities are injected by the owner. Instances are noncopyable and every method
 * must run on the control-owner thread; no callbacks, threads or I/O are retained.
 */
class ExternalAuthority final {
  public:
    /**
     * Copy startup policy and bind this process instance.
     * @param config Validated cycle policy providing timeouts, dwell, standstill and rpm limits.
     * @param app_instance_id Random identity for this process lifetime; empty permanently inhibits.
     */
    ExternalAuthority(const CycleConfig& config, Id128 app_instance_id) noexcept;
    ExternalAuthority(const ExternalAuthority&) = delete;
    ExternalAuthority& operator=(const ExternalAuthority&) = delete;

    /**
     * Register a new verified producer connection and revoke everything older.
     * @return New positive connection generation, or 0 when permanently inhibited/exhausted.
     */
    [[nodiscard]] std::uint64_t connect() noexcept;
    /** Drop the producer connection, outstanding challenge and authorization. */
    void disconnect() noexcept;
    /**
     * Issue the single outstanding challenge for the current connection.
     * @param nonce Owner-generated random challenge identity; empty or repeated values are refused.
     * @param now Board monotonic issue time t0, retained only inside the authority.
     * @return Binding to echo, or empty while disconnected, inhibited or a live challenge exists.
     */
    [[nodiscard]] std::optional<ExternalChallenge> challenge(Id128 nonce, time::MonotonicTime now) noexcept;
    /**
     * Validate one reply; entering validation consumes the outstanding challenge and sequence.
     * @param reply Untrusted copied reply.
     * @param now Board monotonic validation time t1.
     * @return Validation verdict; rejection never refreshes age, qualification or generations.
     */
    [[nodiscard]] ExternalVerdict submit(const ExternalReply& reply, time::MonotonicTime now) noexcept;
    /**
     * Apply per-cycle revocation rules and return the external producer copy for CycleInput.
     * @param sbus Same SBUS snapshot that will be passed to the cycle.
     * @param state Current runtime guard snapshot.
     * @param cycle Sole control owner, queried read-only for consumed authorization.
     * @param shutdown_requested True permanently inhibits.
     * @param now Board monotonic time; regression permanently inhibits.
     * @return Fresh authorized candidate, otherwise an empty unusable sample.
     */
    [[nodiscard]] command::CommandSample update(const command::CommandSample& sbus, const drive::RuntimeState& state,
                                                const ControlCycle& cycle, bool shutdown_requested,
                                                time::MonotonicTime now) noexcept;
    /** Return the current phase for diagnostics; owner thread only. */
    [[nodiscard]] ExternalPhase phase() const noexcept;
    /** Return the current authorization generation, 0 before the first grant; owner thread only. */
    [[nodiscard]] std::uint64_t authorization_generation() const noexcept;

  private:
    /** Return to REVOKED, discarding qualification progress and the forwarded candidate. */
    void revoke() noexcept;
    /** Return whether a monotonic time has not yet reached the external timeout after start. */
    [[nodiscard]] bool within_timeout(time::MonotonicTime start, time::MonotonicTime now) const noexcept;

    const CycleConfig config_;
    const Id128 instance_;
    ExternalChallenge outstanding_{};
    time::MonotonicTime outstanding_at_{};
    Id128 last_nonce_{};
    drive::RuntimeGeneration drive_generation_{};
    command::CommandSample candidate_{};
    time::MonotonicTime neutral_since_{};
    time::MonotonicTime last_neutral_at_{};
    time::MonotonicTime last_now_{};
    std::uint64_t connection_{0};
    std::uint64_t authorization_{0};
    std::uint64_t sequence_floor_{0};
    ExternalPhase phase_{ExternalPhase::revoked};
    bool connected_{false};
    bool challenge_live_{false};
    bool conditions_ok_{false};
    bool have_drive_generation_{false};
    bool have_now_{false};
    bool stopped_{false};
};

} // namespace robot_control::application::control
