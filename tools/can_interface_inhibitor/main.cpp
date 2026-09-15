#include "platform/linux/can/interface_inhibitor.hpp"

#include <charconv>
#include <string>
#include <string_view>

/** Parse and run the fixed-purpose privileged interface inhibitor. */
int main(const int argc, char** argv) {
    if (argc != 5 || std::string_view{argv[1]} != "--control-fd" || std::string_view{argv[3]} != "--interface") {
        return 2;
    }
    int descriptor = -1;
    const std::string_view value{argv[2]};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), descriptor);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        return 2;
    }
    return robot_control::platform::linux::can::run_interface_inhibitor(descriptor, argv[4]);
}
