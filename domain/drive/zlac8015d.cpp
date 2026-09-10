#include "domain/drive/zlac8015d.hpp"

#include <bit>

namespace robot_control::domain::drive::zlac8015d {
namespace {

constexpr std::int32_t target_limit_rpm = 1000;

/** Return an unsigned byte value without an integer promotion surprise. */
std::uint8_t byte_value(const std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
}

/** Load one little-endian 32-bit value from a validated buffer. */
std::uint32_t load_u32(const std::span<const std::byte> data) noexcept {
    return static_cast<std::uint32_t>(byte_value(data[0])) | (static_cast<std::uint32_t>(byte_value(data[1])) << 8U)
           | (static_cast<std::uint32_t>(byte_value(data[2])) << 16U)
           | (static_cast<std::uint32_t>(byte_value(data[3])) << 24U);
}

/** Store one 16-bit value in little-endian order. */
std::array<std::byte, 2> store_u16(const std::uint16_t value) noexcept {
    return {std::byte{static_cast<std::uint8_t>(value)}, std::byte{static_cast<std::uint8_t>(value >> 8U)}};
}

/** Store one 32-bit value in little-endian order. */
std::array<std::byte, 4> store_u32(const std::uint32_t value) noexcept {
    return {std::byte{static_cast<std::uint8_t>(value)}, std::byte{static_cast<std::uint8_t>(value >> 8U)},
            std::byte{static_cast<std::uint8_t>(value >> 16U)}, std::byte{static_cast<std::uint8_t>(value >> 24U)}};
}

/** Return whether a neutral channel names an approved independent subindex. */
bool valid_channel(const IndependentChannel channel) noexcept {
    return channel == IndependentChannel::subindex_1 || channel == IndependentChannel::subindex_2;
}

} // namespace

std::optional<DualAxisStatus> decode_dual_status(const std::span<const std::byte> data) noexcept {
    if (data.size() != 4U) {
        return std::nullopt;
    }
    return decode_dual_axis_status(load_u32(data));
}

std::optional<DualFaultObservation> decode_dual_fault(const std::span<const std::byte> data) noexcept {
    if (data.size() != 4U) {
        return std::nullopt;
    }
    const std::uint32_t raw = load_u32(data);
    return DualFaultObservation{
        .low_half = static_cast<std::uint16_t>(raw),
        .high_half = static_cast<std::uint16_t>(raw >> 16U),
        .raw = raw,
        .physical_axis_mapping_known = false,
    };
}

std::optional<ModeObservation> decode_mode_display(const std::span<const std::byte> data) noexcept {
    if (data.size() != 1U) {
        return std::nullopt;
    }
    const std::int8_t raw = std::bit_cast<std::int8_t>(byte_value(data[0]));
    OperationMode mode = OperationMode::unknown;
    switch (raw) {
        case 1:
            mode = OperationMode::profile_position;
            break;
        case 3:
            mode = OperationMode::profile_velocity;
            break;
        case 4:
            mode = OperationMode::profile_torque;
            break;
        default:
            break;
    }
    return ModeObservation{.mode = mode, .raw = raw};
}

std::optional<IndependentVelocityFeedback> decode_independent_velocity(const IndependentChannel channel,
                                                                       const std::span<const std::byte> data) noexcept {
    if (!valid_channel(channel) || data.size() != 4U) {
        return std::nullopt;
    }
    return IndependentVelocityFeedback{
        .channel = channel,
        .tenths_rpm = std::bit_cast<std::int32_t>(load_u32(data)),
        .physical_axis_mapping_known = false,
    };
}

std::optional<PackedVelocityFeedback> decode_packed_velocity(const std::span<const std::byte> data) noexcept {
    if (data.size() != 4U) {
        return std::nullopt;
    }
    const std::uint32_t raw = load_u32(data);
    return PackedVelocityFeedback{
        .low_half_tenths_rpm = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(raw)),
        .high_half_tenths_rpm = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(raw >> 16U)),
        .raw = raw,
        .physical_axis_mapping_known = false,
    };
}

std::optional<IndependentTarget> decode_independent_target(const IndependentChannel channel,
                                                           const std::span<const std::byte> data) noexcept {
    if (!valid_channel(channel) || data.size() != 4U) {
        return std::nullopt;
    }
    const std::int32_t rpm = std::bit_cast<std::int32_t>(load_u32(data));
    if (rpm < -target_limit_rpm || rpm > target_limit_rpm) {
        return std::nullopt;
    }
    return IndependentTarget{
        .channel = channel,
        .rpm = rpm,
        .physical_axis_mapping_known = false,
    };
}

std::optional<std::array<std::byte, 4>> encode_independent_target(const IndependentChannel channel,
                                                                  const std::int32_t rpm) noexcept {
    if (!valid_channel(channel) || rpm < -target_limit_rpm || rpm > target_limit_rpm) {
        return std::nullopt;
    }
    return store_u32(static_cast<std::uint32_t>(rpm));
}

std::optional<std::array<std::byte, 2>>
encode_transition_controlword(const TransitionControlword controlword) noexcept {
    switch (controlword) {
        case TransitionControlword::disable_voltage:
            return store_u16(0x0000U);
        case TransitionControlword::quick_stop:
            return store_u16(0x0002U);
        case TransitionControlword::shutdown:
            return store_u16(0x0006U);
        case TransitionControlword::switch_on:
            return store_u16(0x0007U);
        case TransitionControlword::enable_operation:
            return store_u16(0x000FU);
    }
    return std::nullopt;
}

} // namespace robot_control::domain::drive::zlac8015d
