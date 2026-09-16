#pragma once
#ifndef ROBOT_CONTROL_CANOPEN_RUNTIME
#error "RuntimeSession requires the isolated CANopen runtime build"
#endif
#include "communication/canopen/lifecycle.hpp"
#include "domain/drive/runtime.hpp"

namespace robot_control::communication::canopen {
/** Caller-supplied readbacks for this transport/boot; P10 obtains them from the peer.
 * This value is not physical verification: creation performs no SDO or remapping. */
struct RuntimeLayoutProof {
    ObservationGeneration generation{};
    std::chrono::steady_clock::time_point verified_at{};
    std::uint32_t rpdo_cob_id{0}, status_cob_id{0}, diagnostics_cob_id{0};
    std::uint8_t rpdo_type{0}, status_type{0}, diagnostics_type{0};
    std::array<std::uint32_t, 2> rpdo_map{}, status_map{}, diagnostics_map{};
    std::uint8_t rpdo_count{0}, status_count{0}, diagnostics_count{0};
    std::uint16_t command_application{0};
};
/** Bind the decision to a runtime session as well as the remote boot. */
struct RuntimeSubmission {
    domain::drive::RuntimeRequest request{};
    std::uint64_t session{0};
};
/** Single-owner adapter borrowing Lifecycle and its socket; neither may be called concurrently.
 * Lifecycle must outlive this session. No threads, SDO/NMT writes or unbounded loop. */
class RuntimeSession final {
  public:
    using CreateResult = platform::linux::Result<std::unique_ptr<RuntimeSession>>;
    using SendResult = platform::linux::Result<domain::drive::RuntimeOutput>;
    /** Validate copied config/readbacks and attach one session. Owner-only; never transmits.
     * @param lifecycle Borrowed owner, required to outlive the returned session.
     * @param config Explicit physical binding and deadlines, copied by value.
     * @param proof Caller-verified layout for this transport/boot generation.
     * @return Owning session or a context-rich failure. */
    [[nodiscard]] static CreateResult create(Lifecycle& lifecycle, domain::drive::RuntimeConfig config,
                                             const RuntimeLayoutProof& proof) noexcept;
    /** Detach and inhibit memory state; call stop() explicitly for bus cleanup. */
    ~RuntimeSession();
    RuntimeSession(const RuntimeSession&) = delete;
    RuntimeSession& operator=(const RuntimeSession&) = delete;
    /** Revalidate and send one guarded RPDO. Owner-only; never retries.
     * @param submission Current session-bound safety envelope borrowed for this call.
     * @return Sent output (possibly rejected/zero), or error. Success is kernel acceptance only. */
    [[nodiscard]] SendResult submit(const RuntimeSubmission& submission) noexcept;
    /** Permanently inhibit and attempt one zero Shutdown if the binding is current. */
    [[nodiscard]] platform::linux::Status stop() noexcept;
    /** Return owned diagnostics without refreshing validity. Owner-only. */
    [[nodiscard]] domain::drive::RuntimeState state() const noexcept;
    /** Return the nonzero owner-lifetime session token. Owner-only. */
    [[nodiscard]] std::uint64_t session() const noexcept;

  private:
    friend class Lifecycle;
    /** Attach validated resources without I/O. */
    RuntimeSession(Lifecycle&, domain::drive::RuntimeConfig, ObservationGeneration, std::uint64_t) noexcept;
    /** Observe every owner event/deadline; send one zero on authority loss. */
    [[nodiscard]] platform::linux::Status refresh(std::chrono::steady_clock::time_point now) noexcept;
    /** Send exactly the evaluated payload once using the owner socket. */
    [[nodiscard]] platform::linux::Status send() noexcept;
    /** Attach operation, interface, node and session identity to an error. */
    [[nodiscard]] platform::linux::Status failure(const char* operation, int code) const noexcept;
    Lifecycle* lifecycle_;
    domain::drive::RuntimePolicy policy_;
    ObservationGeneration generation_;
    std::uint64_t session_;
    std::uint64_t malformed_{0}, replayed_{0}, future_{0};
    bool binding_current_{true};
    bool stop_attempted_{false};
    platform::linux::Status stop_status_{};
};
} // namespace robot_control::communication::canopen
