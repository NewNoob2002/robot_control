#include "application/config/startup.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

/**
 * Verify the shared CANopen profile and each intentional SDO deadline without device access.
 * @return EXIT_SUCCESS only when every previously literal field is preserved.
 * Thread safety: Single test owner; all configuration and results are local values.
 */
int main() {
    using namespace std::chrono_literals;
    using namespace robot_control::application::config;
    Startup startup;
    startup.interface = "vcan_test";
    bool valid = true;
    for (const auto timeout : {250ms, 2000ms}) {
        const auto stack = make_stack_config(startup, timeout);
        valid = valid && stack.interface_name == startup.interface && stack.controller_node_id == 127
                && stack.remote_node_id == 1 && stack.bit_rate_kbit_s == 500 && stack.heartbeat_timeout == 1000ms
                && stack.sdo_timeout == timeout && stack.tpdo_timeout == 200ms
                && stack.tpdo_expected_dlc == std::array<std::uint8_t, 4>{8, 5, 0, 0};
    }
    if (!valid)
        std::cerr << "CANopen startup profile changed\n";
    return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
