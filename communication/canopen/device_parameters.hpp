#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace robot_control::communication::canopen {
/** Reviewed persistent OD field; default -1 means preserve the device value. */
struct DeviceParameter {
    std::string_view key;
    std::uint16_t index;
    std::uint8_t sub;
    int minimum, maximum, initial;
};
inline constexpr std::array device_parameters{
    DeviceParameter{"speed_resolution", 0x2026, 5, 1, 10, 1},
    DeviceParameter{"drive_mode", 0x6060, 0, 3, 3, 3},
    DeviceParameter{"drive_sync", 0x200f, 0, 1, 1, 1},
    DeviceParameter{"drive_quick_stop", 0x605a, 0, 5, 5, 5},
    DeviceParameter{"drive_max_rpm", 0x2008, 0, 1, 1000, -1},
    DeviceParameter{"drive_accel_left_ms", 0x6083, 1, 0, 32767, -1},
    DeviceParameter{"drive_accel_right_ms", 0x6083, 2, 0, 32767, -1},
    DeviceParameter{"drive_decel_left_ms", 0x6084, 1, 0, 32767, -1},
    DeviceParameter{"drive_decel_right_ms", 0x6084, 2, 0, 32767, -1},
    DeviceParameter{"drive_quick_decel_left_ms", 0x6085, 1, 0, 32767, -1},
    DeviceParameter{"drive_quick_decel_right_ms", 0x6085, 2, 0, 32767, -1},
    DeviceParameter{"drive_io_stop", 0x2026, 3, 0, 514, -1},
    DeviceParameter{"drive_start_lock", 0x2007, 0, 0, 1, -1},
    DeviceParameter{"drive_parking", 0x2026, 4, 0, 1, -1},
    DeviceParameter{"drive_direction", 0x2026, 7, 0, 1, -1},
};
/** Value-owned configuration; no live transport or hardware ownership. */
struct DeviceConfiguration {
    std::array<int, device_parameters.size()> values = [] {
        std::array<int, device_parameters.size()> result{};
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] = device_parameters[i].initial;
        return result;
    }();
    bool configure_pdo{false}; // Explicit opt-in to establish the fixed app-compatible baseline.
};
/** Validate every supplied value, including the two independent I/O stop bytes. Pure and reentrant. */
[[nodiscard]] inline bool valid_device_configuration(const DeviceConfiguration& config) noexcept {
    for (std::size_t i = 0; i < config.values.size(); ++i) {
        const auto value = config.values[i];
        const auto& p = device_parameters[i];
        if (value == -1 && p.initial == -1)
            continue;
        if (value < p.minimum || value > p.maximum)
            return false;
        if (p.index == 0x2026 && p.sub == 3 && ((value & 255) > 2 || (value >> 8) > 2))
            return false;
    }
    return true;
}
} // namespace robot_control::communication::canopen
