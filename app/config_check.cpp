#include "application/config/startup.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

/** Validate startup configuration without opening UART/CAN/socket or creating logs; exit2 rejects configuration. */
int main(int argc, char* argv[]) {
    constexpr std::string_view usage = "Usage: robot-control-config-check --config FILE [--set key=value ...]\n";
    if (argc == 2 && std::string_view{argv[1]} == "--help") {
        std::cout << usage;
        return 0;
    }
    if (argc < 3 || argc > 23 || argc % 2 == 0 || std::string_view{argv[1]} != "--config") {
        std::cerr << usage;
        return 2;
    }
    try {
        std::vector<std::string_view> overrides;
        for (int index = 3; index < argc; index += 2) {
            if (std::string_view{argv[index]} != "--set")
                throw std::invalid_argument("expected --set key=value");
            overrides.emplace_back(argv[index + 1]);
        }
        std::ifstream input{argv[2]};
        if (!input)
            throw std::runtime_error("cannot open configuration file");
        const auto config = robot_control::application::config::parse(input, overrides);
        robot_control::application::config::dump(std::cout, config);
        std::cout << "# CONFIG_VALID_DEVICE_ACCESS_NONE; not a hardware preflight or motion permit\n";
        return std::cout.good() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "configuration_error: " << error.what() << '\n';
        return 2;
    }
}
