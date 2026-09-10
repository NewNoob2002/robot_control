#pragma once

#include "domain/drive/cia402.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace robot_control::domain::drive::zlac8015d {

/** Identify an independent vendor subindex without assigning a physical axis. */
enum class IndependentChannel : std::uint8_t {
    subindex_1 = 1,
    subindex_2 = 2,
};

/** Preserve the neutral low/high halves of vendor fault object 0x603f. */
struct DualFaultObservation {
    std::uint16_t low_half{0};
    std::uint16_t high_half{0};
    std::uint32_t raw{0};
    bool physical_axis_mapping_known{false};
};

/** Supported operation modes decoded from object 0x6061. */
enum class OperationMode : std::uint8_t {
    unknown = 0,
    profile_position,
    profile_velocity,
    profile_torque,
};

/** Retain a decoded operation mode and its signed raw value. */
struct ModeObservation {
    OperationMode mode{OperationMode::unknown};
    std::int8_t raw{0};
};

/** Preserve one independent 0x606c velocity value in 0.1 rpm units. */
struct IndependentVelocityFeedback {
    IndependentChannel channel{IndependentChannel::subindex_1};
    std::int32_t tenths_rpm{0};
    bool physical_axis_mapping_known{false};
};

/** Preserve packed low/high 0x606c velocity halves in 0.1 rpm units. */
struct PackedVelocityFeedback {
    std::int16_t low_half_tenths_rpm{0};
    std::int16_t high_half_tenths_rpm{0};
    std::uint32_t raw{0};
    bool physical_axis_mapping_known{false};
};

/** Preserve one independent 0x60ff target in whole rpm. */
struct IndependentTarget {
    IndependentChannel channel{IndependentChannel::subindex_1};
    std::int32_t rpm{0};
    bool physical_axis_mapping_known{false};
};

/** Reviewed CiA402 transition controlwords permitted by Phase 6. */
enum class TransitionControlword : std::uint8_t {
    disable_voltage,
    quick_stop,
    shutdown,
    switch_on,
    enable_operation,
};

/**
 * Decode the vendor 32-bit dual status object using neutral half names.
 *
 * @param data Borrowed little-endian object bytes, valid only for this call.
 * @return Decoded status when the width is exactly four bytes; otherwise empty.
 *
 * Thread safety: Pure and reentrant. No ownership is retained.
 */
[[nodiscard]] std::optional<DualAxisStatus> decode_dual_status(std::span<const std::byte> data) noexcept;

/**
 * Decode the vendor 32-bit dual fault object using neutral half names.
 *
 * @param data Borrowed little-endian object bytes, valid only for this call.
 * @return Decoded fault halves when width is four bytes; otherwise empty.
 *
 * Thread safety: Pure and reentrant. No ownership is retained.
 */
[[nodiscard]] std::optional<DualFaultObservation> decode_dual_fault(std::span<const std::byte> data) noexcept;

/**
 * Decode the signed operation-mode display from object 0x6061.
 *
 * @param data Borrowed object bytes, valid only for this call.
 * @return Known or unknown mode when width is one byte; otherwise empty.
 *
 * Thread safety: Pure and reentrant. No ownership is retained.
 */
[[nodiscard]] std::optional<ModeObservation> decode_mode_display(std::span<const std::byte> data) noexcept;

/**
 * Decode an independent signed 0x606c feedback value.
 *
 * @param channel Neutral object subindex identifier.
 * @param data Borrowed little-endian object bytes, valid only for this call.
 * @return Feedback when channel and four-byte width are valid; otherwise empty.
 *
 * Thread safety: Pure and reentrant. No ownership is retained.
 */
[[nodiscard]] std::optional<IndependentVelocityFeedback>
decode_independent_velocity(IndependentChannel channel, std::span<const std::byte> data) noexcept;

/**
 * Decode packed signed low/high 0x606c feedback halves.
 *
 * @param data Borrowed little-endian object bytes, valid only for this call.
 * @return Packed feedback when width is four bytes; otherwise empty.
 *
 * Thread safety: Pure and reentrant. No ownership is retained.
 */
[[nodiscard]] std::optional<PackedVelocityFeedback> decode_packed_velocity(std::span<const std::byte> data) noexcept;

/**
 * Decode a bounded independent 0x60ff target value.
 *
 * @param channel Neutral object subindex identifier.
 * @param data Borrowed little-endian object bytes, valid only for this call.
 * @return Target within -1000..1000 rpm when valid; otherwise empty.
 *
 * Thread safety: Pure and reentrant. No ownership is retained.
 */
[[nodiscard]] std::optional<IndependentTarget> decode_independent_target(IndependentChannel channel,
                                                                         std::span<const std::byte> data) noexcept;

/**
 * Encode a bounded independent 0x60ff target value.
 *
 * @param channel Neutral object subindex identifier.
 * @param rpm Target in whole rpm within -1000..1000.
 * @return Four little-endian bytes when valid; otherwise empty.
 *
 * Thread safety: Pure and reentrant. The returned array owns its bytes.
 */
[[nodiscard]] std::optional<std::array<std::byte, 4>> encode_independent_target(IndependentChannel channel,
                                                                                std::int32_t rpm) noexcept;

/**
 * Encode one reviewed transition controlword.
 *
 * @param controlword Typed Phase 6 transition request.
 * @return Two little-endian bytes, or empty for an invalid enum value.
 *
 * Thread safety: Pure and reentrant. The returned array owns its bytes.
 */
[[nodiscard]] std::optional<std::array<std::byte, 2>>
encode_transition_controlword(TransitionControlword controlword) noexcept;

} // namespace robot_control::domain::drive::zlac8015d
