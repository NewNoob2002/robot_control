#pragma once

#include "communication/canopen/stack_config.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace robot_control::communication::canopen {

/** Identify one transport lifetime and one remote boot within that lifetime. */
struct ObservationGeneration {
    std::uint64_t transport{0};
    std::uint64_t boot{0};

    [[nodiscard]] bool operator==(const ObservationGeneration&) const = default;
};

/** Preserve one received Classical CAN frame without decoding vendor meaning. */
struct RawCanopenFrame {
    std::uint32_t identifier{0};
    std::uint8_t dlc{0};
    std::array<std::uint8_t, 8> payload{};
    std::chrono::steady_clock::time_point received_at{};
    ObservationGeneration generation{};
};

/** Store raw frame presence and whether it belongs to the current observation. */
struct FrameObservation {
    bool present{false};
    bool current{false};
    RawCanopenFrame raw{};
};

/** Remote NMT states accepted from a one-byte heartbeat or boot-up frame. */
enum class RemoteNmtState : std::uint8_t {
    initializing = 0x00U,
    stopped = 0x04U,
    operational = 0x05U,
    pre_operational = 0x7FU,
    unknown = 0xFFU,
};

/** Store the latest valid remote NMT state and its raw source frame. */
struct NmtObservation {
    bool present{false};
    bool current{false};
    RemoteNmtState state{RemoteNmtState::unknown};
    RawCanopenFrame raw{};
};

/** Store the latest valid heartbeat frame. */
struct HeartbeatObservation {
    FrameObservation frame{};
};

/** Store raw and protocol-level fields from one valid EMCY frame. */
struct EmergencyObservation {
    FrameObservation frame{};
    std::uint16_t error_code{0};
    std::uint8_t error_register{0};
    std::uint8_t error_bit{0};
    std::uint32_t info_code{0};
};

/** Immutable coherent observation value returned to readers. */
struct ObservationSnapshot {
    std::uint64_t version{0};
    ObservationGeneration generation{};
    bool boot_observed{false};
    FrameObservation boot{};
    NmtObservation nmt{};
    HeartbeatObservation heartbeat{};
    EmergencyObservation emergency{};
    FrameObservation sdo_result{};
    std::array<FrameObservation, 4> tpdo{};
    FrameObservation malformed{};
    FrameObservation can_error{};
    std::uint64_t malformed_count{0};
    std::uint64_t future_timestamp_count{0};
    std::uint64_t replay_count{0};
};

/** Accumulate owner-thread CANopen frames and publish coherent value copies. */
class ObservationStore final {
  public:
    /**
   * Construct an inactive store from already validated startup configuration.
   *
   * @param config Configuration copied for node, DLC, and freshness checks.
   *
   * Thread safety: Construct before sharing the object with readers.
   */
    explicit ObservationStore(const StackConfig& config) noexcept;

    /**
   * Start a new transport generation and invalidate all remote observations.
   *
   * Thread safety: Safe for the sole writer while readers call snapshot().
   */
    void begin_transport() noexcept;

    /**
   * Return the generation to stamp on the next owner-received frame.
   *
   * @return Current transport and boot generation.
   *
   * Thread safety: Safe for one writer and concurrent snapshot readers.
   */
    [[nodiscard]] ObservationGeneration generation() const noexcept;

    /**
   * Validate and accumulate one raw frame without invoking external policy.
   *
   * @param frame Raw frame stamped with the generation active at reception.
   * @param now Owner monotonic time captured for this receive operation.
   *
   * Thread safety: Safe for the sole writer while readers call snapshot().
   */
    void ingest(RawCanopenFrame frame, std::chrono::steady_clock::time_point now) noexcept;

    /**
     * Apply heartbeat and TPDO timeout transitions at owner monotonic time.
     *
     * @param now Current owner monotonic time.
     *
     * Thread safety: Safe for the sole writer while readers call snapshot().
     */
    void advance_time(std::chrono::steady_clock::time_point now) noexcept;

    /**
   * Copy one coherent snapshot and apply freshness at the supplied time.
   *
   * @param now Reader monotonic time used for future and timeout checks.
   * @return Immutable value copy with stale records marked non-current.
   *
   * Thread safety: Safe for concurrent readers and the sole writer.
   */
    [[nodiscard]] ObservationSnapshot snapshot(std::chrono::steady_clock::time_point now) const noexcept;

  private:
    /** Invalidate every protocol record while retaining raw diagnostics. */
    void invalidate_remote() noexcept;

    /** Invalidate heartbeat-dependent state while retaining raw values. */
    void invalidate_heartbeat_dependents() noexcept;

    /** Record one malformed frame and invalidate its addressed protocol slot. */
    void record_malformed(const RawCanopenFrame& frame) noexcept;

    /** Invalidate the protocol record selected by a standard CAN identifier. */
    void invalidate_addressed(std::uint32_t identifier) noexcept;

    std::uint8_t remote_node_id_{0};
    std::chrono::milliseconds heartbeat_timeout_{0};
    std::chrono::milliseconds tpdo_timeout_{0};
    std::array<std::uint8_t, 4> tpdo_expected_dlc_{};
    mutable std::mutex mutex_{};
    ObservationSnapshot state_{};
};

} // namespace robot_control::communication::canopen
