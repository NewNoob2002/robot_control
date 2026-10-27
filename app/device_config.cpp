#include <fstream>
#include <iostream>
#include "app/interface_lock.hpp"
#include "application/config/startup.hpp"
#include "communication/canopen/control_qualification.hpp"
#include "communication/canopen/control_write_gate.hpp"

namespace rc = robot_control;

// GNU linker ABI names are required.
// NOLINTNEXTLINE(bugprone-reserved-identifier)
extern "C" ssize_t __real_send(int, const void*, size_t, int);
/** Restrict this executable to reviewed configuration SDOs, Pre-operational NMT and one save.
 * No RPDO, enable, operational/reset NMT or target writes are admitted. Reentrant, no retained data. */
// NOLINTNEXTLINE(bugprone-reserved-identifier)
extern "C" ssize_t __wrap_send(int fd, const void* data, size_t size, int flags) {
    can_frame frame{};
    if (data && size == CAN_MTU && flags == MSG_DONTWAIT) {
        std::memcpy(&frame, data, sizeof(frame));
        if (frame.can_id == 0 && frame.can_dlc == 2 && frame.data[0] == 0x80 && frame.data[1] == 1)
            return __real_send(fd, data, size, flags);
        if (frame.can_id == 0x601 && frame.can_dlc == 8) {
            const auto index = static_cast<std::uint16_t>(frame.data[1] | (static_cast<unsigned>(frame.data[2]) << 8U));
            std::uint32_t value = 0;
            for (unsigned i = 0; i < 4; ++i)
                value |= static_cast<std::uint32_t>(frame.data[4 + i]) << (8U * i);
            const auto width = static_cast<std::uint8_t>(frame.data[0] == 0x2f   ? 1
                                                         : frame.data[0] == 0x2b ? 2
                                                         : frame.data[0] == 0x23 ? 4
                                                                                 : 0);
            const bool read = frame.data[0] == 0x40 && rc::communication::canopen::control_setup_allowed(frame);
            const bool write = robot_control_canopen_device_download_allowed({index, frame.data[3]}, value, width)
                               || (index == 0x2010 && frame.data[3] == 0 && width == 2 && value == 1);
            if (read || write)
                return __real_send(fd, data, size, flags);
        }
    }
    errno = EACCES;
    return -1;
}

/** Parse the shared external profile; default is device-free validation, --apply writes EEPROM.
 * Owns the CAN lifecycle and cooperative lock synchronously; never starts control or reboots. */
int main(int argc, char* argv[]) {
    constexpr auto usage =
        "Usage: robot-control-device-config --config FILE [--apply]\n"
        "Default: validate file only. --apply writes reviewed parameters and saves all drive RW parameters.\n"
        "Stop the app first. Restart the drive manually after success; do not enable motion here.\n";
    if (argc == 2 && std::string_view{argv[1]} == "--help") {
        std::cout << usage;
        return 0;
    }
    if ((argc != 3 && argc != 4) || std::string_view{argv[1]} != "--config"
        || (argc == 4 && std::string_view{argv[3]} != "--apply")) {
        std::cerr << usage;
        return 2;
    }
    rc::application::config::Startup config;
    try {
        std::ifstream file{argv[2]};
        if (!file)
            throw std::invalid_argument("cannot open configuration file");
        config = rc::application::config::parse(file);
    } catch (const std::exception& error) {
        std::cerr << "configuration_error: " << error.what() << '\n';
        return 2;
    }
    rc::application::config::dump(std::cout, config);
    if (argc == 3) {
        std::cout << "CONFIG_VALID_DEVICE_ACCESS_NONE\n";
        return 0;
    }
    try {
        auto lock = rc::app::check_interface(config.interface);
        auto termination = rc::platform::linux::process::TerminationEvent::create();
        if (!termination.ok())
            throw std::runtime_error("termination setup failed");
        using namespace std::chrono_literals;
        const auto stack = rc::application::config::make_stack_config(config, 2000ms);
        auto created = rc::communication::canopen::Lifecycle::create(stack, termination.value());
        auto status = created.status();
        if (created.ok()) {
            auto owner = std::move(created).value();
            rc::communication::canopen::ControlQualification setup{*owner};
            status = setup.configure_device(config.drive);
        }
        std::cout << "event=device_configuration ok=" << status.ok() << " operation=" << status.operation
                  << " context=" << status.context << " errno=" << status.error.value() << '\n';
        if (!status.ok()) {
            std::cerr << "CONFIGURATION_UNVERIFIED: do not start motion or automatically retry saving.\n";
            return 1;
        }
        std::cout << "EEPROM_SAVE_ACKNOWLEDGED RESTART_REQUIRED\n"
                     "请重启驱动器，再启动app。保存应答不证明掉电持久性或实际速度缩放已验证。\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "device_configuration_error: " << error.what() << '\n';
        return 1;
    }
}
