#pragma once

#include "domain/command/command_sample.hpp"
#include "input/sbus/protocol/parser.hpp"

#include <array>
#include <cstdint>
#include <mutex>
#include <span>

namespace robot_control::input::sbus {
using domain::time::Duration;
using domain::time::MonotonicTime;

/** Explicit per-axis calibration; zero initialization is deliberately invalid. */
struct AxisConfig {
    std::uint16_t minimum{0};
    std::uint16_t center{0};
    std::uint16_t maximum{0};
    bool reversed{false};
};

/** Startup-only profile, copied and validated before accepting observations. */
struct SourceConfig {
    std::array<std::uint8_t, 4> channels{0, 2, 5, 6}; // Steering, throttle, button, gear.
    AxisConfig steering{};
    AxisConfig throttle{};
    std::int32_t deadband{50};
    std::uint16_t button_release{500};
    std::uint16_t button_press{1500};
    std::uint16_t gear_low{500};
    std::uint16_t gear_high{1500};
    std::array<std::int32_t, 3> gear_rpm{30, 60, 100};
    std::int32_t maximum_rpm{100};
    std::int32_t output_deadband_rpm{3};
    std::uint32_t recovery_frames{3};
    Duration timeout{std::chrono::milliseconds{100}};
    Duration button_cooldown{std::chrono::milliseconds{300}};
};

enum class ConfigError : std::uint8_t { none, channels, calibration, deadband, thresholds, limits, timing };

/**
 * Validate all profile fields without device access.
 * @param config Borrowed startup profile.
 * @return First invalid field group, or none. Pure, reentrant; retains nothing.
 */
[[nodiscard]] ConfigError validate(const SourceConfig& config) noexcept;

enum class Health : std::uint8_t { startup, recovering, disabled, enabled, faulted };
enum class InputFault : std::uint8_t {
    none,
    configuration,
    rejected,
    frame_lost,
    failsafe,
    timeout,
    future_time,
    replay,
    transport,
    discontinuity,
    session_changed,
    counter_exhausted,
    malformed,
    clock_regression,
    shutdown
};

/** Owned diagnostic and command value; candidates never grant drive authority. */
struct SourceSnapshot {
    domain::command::CommandSample sample{};
    protocol::Frame raw{};
    domain::command::MotionCommand candidate{};
    std::int32_t steering{0};
    std::int32_t throttle{0};
    std::uint32_t recovery_count{0};
    Health health{Health::startup};
    InputFault fault{InputFault::none};
    InputFault last_fault{InputFault::none};
    std::uint8_t last_fault_flags{0}; // Retain flagged-frame cause even after later healthy frames.
    MonotonicTime last_fault_at{};
    ConfigError config_error{ConfigError::none};
    bool has_frame{false};
};

/** Borrowed ordered events from exactly one read; the source retains no spans. */
struct Observation {
    std::span<const protocol::ParseResult> events{};
    MonotonicTime captured_at{};
    std::uint64_t session{0};
    bool discontinuous{false};
};

/**
 * One producer, coherent value snapshots, no clock reads or drive dependencies.
 * All methods lock one mutex; snapshot readers may run concurrently with the
 * single producer. Caller must tick during silence and never destroy concurrently.
 * Recovery counts at most one healthy frame per distinct read timestamp; a fault
 * suppresses recovery and button edges for the rest of that read.
 */
class Source final {
  public:
    /** Copy and validate an explicit profile; invalid configuration stays inhibited. */
    explicit Source(SourceConfig config);
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;

    /**
     * Consume a complete ordered read and atomically publish its resulting value.
     * @param observation Borrowed batch, at most 256 events, with receive identity.
     * @param now Injected nonnegative monotonic time, never earlier than reception.
     * @return Owned snapshot. Old/duplicate batches revoke instead of refreshing.
     * Thread safety: Single producer; concurrent snapshot() is supported.
     */
    [[nodiscard]] SourceSnapshot consume(Observation observation, MonotonicTime now);

    /**
     * Expire silent input at age >= timeout without refreshing receive time.
     * @param now Injected monotonic time.
     * @return Owned snapshot; unchanged polling preserves sequence and content.
     * Thread safety: Single producer; concurrent snapshot() is supported.
     */
    [[nodiscard]] SourceSnapshot tick(MonotonicTime now);

    /**
     * Revoke after UART error/cancellation; require a strictly newer session.
     * @param now Injected monotonic time.
     * @return Owned zero/invalid snapshot. Single producer, concurrent readers safe.
     */
    [[nodiscard]] SourceSnapshot transport_error(MonotonicTime now);

    /**
     * Inhibit on normal deadline or process shutdown and require a new session.
     * @param now Injected monotonic time.
     * @return Owned invalid/zero value. Single producer; concurrent readers safe.
     */
    [[nodiscard]] SourceSnapshot stop(MonotonicTime now);

    /** Return an owned coherent copy; concurrent with all producer methods. */
    [[nodiscard]] SourceSnapshot snapshot() const;

  private:
    friend struct SourceTestAccess; // Only the unit test defines counter-boundary access.
    /** Revoke and retain the last cause; caller holds mutex_. */
    void revoke(InputFault fault, MonotonicTime now);
    /** Validate producer time and expire old state; caller holds mutex_. */
    bool advance_time(MonotonicTime now);
    /** Publish a new revision, reserving the final sequence for terminal inhibition. */
    void publish();
    /** Map one validated frame and handle eligible button edges under mutex_. */
    void accept_frame(const protocol::Frame& frame, MonotonicTime received, bool eligible, bool count_recovery);

    const SourceConfig config_;
    mutable std::mutex mutex_{};
    SourceSnapshot state_{};
    MonotonicTime last_now_{};
    MonotonicTime last_batch_{};
    MonotonicTime last_toggle_{};
    bool have_now_{false};
    bool have_batch_{false};
    bool have_toggle_{false};
    bool button_down_{false};
    bool released_{false};
    bool require_new_session_{false};
    bool terminal_{false};
};
} // namespace robot_control::input::sbus
