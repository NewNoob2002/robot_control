#include <iostream>
#include "communication/canopen/control_write_gate.hpp"
#include "communication/canopen/device_parameters.hpp"

/** Verify that provisioning permissions never leak into the runtime send gate; no device access. */
int main() {
    namespace co = robot_control::communication::canopen;
    for (const auto& p : co::device_parameters) {
        const auto width = robot_control_canopen_qualification_upload_size({p.index, p.sub});
        can_frame frame{};
        frame.can_id = 0x601;
        frame.can_dlc = 8;
        frame.data[0] = width == 1 ? 0x2f : width == 2 ? 0x2b : 0x23;
        frame.data[1] = static_cast<std::uint8_t>(p.index);
        frame.data[2] = static_cast<std::uint8_t>(p.index >> 8U);
        frame.data[3] = p.sub;
        const auto value = static_cast<std::uint32_t>(p.minimum);
        for (unsigned i = 0; i < 4; ++i)
            frame.data[4 + i] = static_cast<std::uint8_t>(value >> (8U * i));
        if (co::control_setup_allowed(frame)
            || !robot_control_canopen_device_download_allowed({p.index, p.sub}, value, width))
            return 1;
    }
    if (robot_control_canopen_device_download_allowed({0x6040, 0}, 15, 2)
        || robot_control_canopen_device_download_allowed({0x60ff, 3}, 1, 4)
        || robot_control_canopen_device_download_allowed({0x200a, 0}, 2, 2)
        || robot_control_canopen_device_download_allowed({0x200b, 0}, 0, 2)
        || robot_control_canopen_device_download_allowed({0x2009, 0}, 1, 2)
        || robot_control_canopen_device_download_allowed({0x2026, 3}, 259, 2)
        || robot_control_canopen_device_download_allowed({0x6083, 1}, 32768, 4)
        || robot_control_canopen_device_download_allowed({0x6083, 1}, 500, 2)
        || robot_control_canopen_device_download_allowed({0x2008, 1}, 100, 2)
        || robot_control_canopen_device_download_allowed({0x605a, 0}, 7, 2)
        || robot_control_canopen_device_download_allowed({0x6060, 0}, 1, 1))
        return 2;
    can_frame save{};
    save.can_id = 0x601;
    save.can_dlc = 8;
    save.data[0] = 0x2b;
    save.data[1] = 0x10;
    save.data[2] = 0x20;
    save.data[4] = 1;
    if (co::control_setup_allowed(save))
        return 3;
    std::cout << "PASS: persistent parameters/save denied at runtime; unrelated writes and invalid widths rejected\n";
}
