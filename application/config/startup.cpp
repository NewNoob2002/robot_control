#include "application/config/startup.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <istream>
#include <map>
#include <ostream>
#include <set>
#include <stdexcept>

namespace robot_control::application::config {
namespace {
using namespace std::chrono_literals;
constexpr std::array<std::string_view, 11> keys{"profile",   "interface",        "device",           "log_dir",
                                                "max_rpm",   "period_ms",        "run_duration_ms",  "log_file_bytes",
                                                "log_files", "speed_resolution", "drive_pdo_profile"};

/** Trim configuration whitespace without changing internal path characters. */
std::string_view trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string_view::npos)
        return {};
    return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}

/** Recognize only the fixed app keys and reviewed persistent device keys. Pure and reentrant. */
bool known_key(std::string_view key) {
    return std::find(keys.begin(), keys.end(), key) != keys.end()
           || std::any_of(communication::canopen::device_parameters.begin(),
                          communication::canopen::device_parameters.end(), [key](const auto& entry) {
                              return entry.key == key;
                          });
}

/** Split one bounded, known field; reject control characters and shell expansion syntax. */
std::pair<std::string, std::string> field(std::string_view line) {
    const auto separator = line.find('=');
    const auto key = trim(line.substr(0, separator));
    const auto value = separator == std::string_view::npos ? std::string_view{} : trim(line.substr(separator + 1));
    if (line.size() > 1024 || !known_key(key) || value.empty()
        || !std::all_of(value.begin(), value.end(),
                        [](char c) {
                            return c >= 33 && c <= 126;
                        })
        || value.find_first_of("$`\"'\\#=") != std::string_view::npos)
        throw std::invalid_argument("invalid configuration field: " + std::string{key});
    return {std::string{key}, std::string{value}};
}

/** Parse a whole decimal integer with explicit bounds, rejecting suffixes and overflow. */
int number(const std::map<std::string, std::string>& values, const char* key, int minimum, int maximum) {
    const auto& text = values.at(key);
    int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value < minimum || value > maximum)
        throw std::invalid_argument(std::string{"out-of-range integer: "} + key);
    return value;
}
} // namespace

Startup parse(std::istream& input, std::span<const std::string_view> overrides) {
    std::map<std::string, std::string> values;
    std::string line;
    std::size_t bytes = 0;
    // Read incrementally: an unbounded line must not allocate unbounded startup memory.
    for (char c = 0; input.get(c);) {
        if (++bytes > 16384 || line.size() >= 1024)
            throw std::invalid_argument("configuration exceeds size limit");
        if (c != '\n') {
            line += c;
            continue;
        }
        const auto content = trim(line);
        if (!content.empty() && content.front() != '#') {
            auto [key, value] = field(content);
            if (!values.emplace(key, value).second)
                throw std::invalid_argument("duplicate configuration key: " + key);
        }
        line.clear();
    }
    if (input.bad() || !input.eof())
        throw std::runtime_error("configuration read failed");
    const auto final = trim(line);
    if (!final.empty() && final.front() != '#') {
        auto [key, value] = field(final);
        if (!values.emplace(key, value).second)
            throw std::invalid_argument("duplicate configuration key: " + key);
    }
    if (overrides.size() > keys.size() + communication::canopen::device_parameters.size())
        throw std::invalid_argument("too many configuration overrides");
    std::set<std::string> overridden;
    for (auto text : overrides) {
        auto [key, value] = field(text);
        if (!overridden.insert(key).second)
            throw std::invalid_argument("duplicate override: " + key);
        values[key] = value;
    }
    values.try_emplace("speed_resolution", "1"); // Existing configuration files retain whole-rpm behavior.
    values.try_emplace("drive_pdo_profile", "0");
    for (auto key : keys)
        if (!values.contains(std::string{key}))
            throw std::invalid_argument("missing configuration key: " + std::string{key});
    if (values.at("profile") != "rk3588-zlac8015d-v1")
        throw std::invalid_argument("unsupported hardware profile");
    Startup config;
    config.interface = values.at("interface");
    config.device = values.at("device");
    config.log_dir = values.at("log_dir");
    if (config.interface.empty() || config.interface.size() > 15
        || !std::all_of(config.interface.begin(), config.interface.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'
                      || c == '-';
           }))
        throw std::invalid_argument("invalid CAN interface name");
    if (!config.device.starts_with("/dev/") || config.device.size() <= 5
        || config.device.find("/../") != std::string::npos || config.device.ends_with("/.."))
        throw std::invalid_argument("device must name a path under /dev");
    const int max_rpm = number(values, "max_rpm", 1, 100);
    config.cycle.period = std::chrono::milliseconds{number(values, "period_ms", 1, 20)};
    config.run_duration = std::chrono::milliseconds{number(values, "run_duration_ms", 0, 3600000)};
    config.log_file_bytes = static_cast<std::size_t>(number(values, "log_file_bytes", 65536, 1073741824));
    config.log_files = static_cast<unsigned>(number(values, "log_files", 1, 20));
    config.cycle.runtime = {domain::drive::PackedHalf::low,
                            domain::drive::PackedHalf::low,
                            1,
                            -1,
                            max_rpm,
                            20,
                            1000ms,
                            200ms,
                            50ms,
                            0x8000};
    for (std::size_t i = 0; i < config.drive.values.size(); ++i) {
        const auto& parameter = communication::canopen::device_parameters[i];
        const std::string key{parameter.key};
        values.try_emplace(key, parameter.initial == -1 ? "keep" : std::to_string(parameter.initial));
        config.drive.values[i] = values.at(key) == "keep" && parameter.initial == -1
                                     ? -1
                                     : number(values, key.c_str(), parameter.minimum, parameter.maximum);
        if (parameter.index == 0x2008 && config.drive.values[i] != -1 && max_rpm > config.drive.values[i])
            throw std::invalid_argument("max_rpm exceeds drive_max_rpm");
    }
    config.drive.configure_pdo = number(values, "drive_pdo_profile", 0, 1) == 1;
    if (!communication::canopen::valid_device_configuration(config.drive))
        throw std::invalid_argument("invalid device parameter value");
    config.cycle.runtime.speed_resolution = config.drive.values[0];
    config.cycle.runtime.powered_standby = true;
    config.cycle.arbiter.max_abs_rpm = max_rpm;
    config.cycle.transition_timeout = 1000ms;
    config.sbus.steering = {200, 1000, 1800, false};
    config.sbus.throttle = {200, 993, 1800, false};
    config.sbus.output_deadband_rpm = 1;
    config.sbus.gear_rpm = {std::max(1, max_rpm * 3 / 10), std::max(1, max_rpm * 6 / 10), max_rpm};
    config.sbus.maximum_rpm = max_rpm;
    if (!control::valid_cycle_config(config.cycle)
        || input::sbus::validate(config.sbus) != input::sbus::ConfigError::none)
        throw std::invalid_argument("invalid control or SBUS configuration");
    return config;
}

void dump(std::ostream& output, const Startup& config) {
    output << "# ===== Driver parameters: provision/save/restart; app read-only =====\n";
    for (std::size_t i = 0; i < config.drive.values.size(); ++i)
        output << communication::canopen::device_parameters[i].key << '='
               << (config.drive.values[i] == -1 ? "keep" : std::to_string(config.drive.values[i])) << '\n';
    output << "drive_pdo_profile=" << config.drive.configure_pdo << "\n# ===== App parameters =====\n";
    output << "profile=rk3588-zlac8015d-v1\ninterface=" << config.interface << "\ndevice=" << config.device
           << "\nlog_dir=" << config.log_dir << "\nmax_rpm=" << config.cycle.runtime.max_abs_rpm
           << "\nperiod_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(config.cycle.period).count()
           << "\nrun_duration_ms=" << config.run_duration.count() << "\nlog_file_bytes=" << config.log_file_bytes
           << "\nlog_files=" << config.log_files
           << "\n# Fixed profile: controller127 node1 target/feedback left=low/+1 right=high/-1; X1=0x8000"
           << "\n# CH1/3/6/7; steering=200/1000/1800 throttle=200/993/1800; neither reversed"
           << "\n# deadband=50 output_deadband_rpm=" << config.sbus.output_deadband_rpm
           << "; gear_ratios=0.3,0.6,1.0; integer_rpm_floor_min1; standstill_tenths_rpm=20; idle_fault_tenths_rpm=50"
           << "\n# CH7 gear_rpm=" << config.sbus.gear_rpm[0] << ',' << config.sbus.gear_rpm[1] << ','
           << config.sbus.gear_rpm[2] << "\n# gear_full_throttle_rpm="
           << (config.sbus.gear_rpm[0] < config.sbus.output_deadband_rpm ? 0 : config.sbus.gear_rpm[0]) << ','
           << (config.sbus.gear_rpm[1] < config.sbus.output_deadband_rpm ? 0 : config.sbus.gear_rpm[1]) << ','
           << (config.sbus.gear_rpm[2] < config.sbus.output_deadband_rpm ? 0 : config.sbus.gear_rpm[2])
           << "\n# HB=1000ms TPDO=200ms decision=50ms SBUS=100ms zero_dwell=150ms transition=1000ms"
           << "\n# button=500/1500 cooldown=300ms recovery_frames=3 gear_thresholds=500/1500"
           << "\n# powered_standby=1; CH6=motion_authority; absent/disabled SBUS=zero_speed_servo_hold"
           << "\n# SBUS-only; IPC deferred; UART=100000/8E2/PARMRK; run_duration_ms=0 means until signal\n";
}
} // namespace robot_control::application::config
