#include "communication/canopen/lifecycle.hpp"
#include "communication/canopen/stack_config.hpp"
#include "platform/linux/process/termination_event.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace std::chrono_literals;
using robot_control::communication::canopen::FrameObservation;
using robot_control::communication::canopen::Lifecycle;
using robot_control::communication::canopen::LifecycleExit;
using robot_control::communication::canopen::ObservationSnapshot;
using robot_control::communication::canopen::RawCanopenFrame;
using robot_control::communication::canopen::StackConfig;
using robot_control::communication::canopen::validate_stack_config;
using robot_control::platform::linux::Result;
using robot_control::platform::linux::Status;
using robot_control::platform::linux::process::TerminationEvent;

constexpr auto maximum_duration = 60s;
constexpr auto observation_step = 10ms;

struct Options {
    StackConfig stack;
    std::chrono::milliseconds duration{1000};
};

/** Print the explicit normal-observer configuration syntax. */
void print_usage() {
    std::cout << "Usage: robot-control-canopen-observer --interface IFACE --controller-node N --remote-node N \\n"
                 "       --bitrate-kbit N --heartbeat-timeout-ms N --sdo-timeout-ms N \\n"
                 "       --tpdo-timeout-ms N --tpdo-dlc D1,D2,D3,D4 [--duration-ms N]\n";
}

/** Return one context-rich command-line parsing failure. */
Result<Options> argument_error(std::string context, const int error_number = EINVAL) {
    return Result<Options>::failure(Status::from_errno("parse_arguments", std::move(context), error_number));
}

/** Parse one complete unsigned decimal token without truncation. */
template <typename T> bool parse_unsigned(const std::string_view text, T& value) {
    std::uint64_t parsed_value = 0U;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), parsed_value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()
        || parsed_value > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
        return false;
    }
    value = static_cast<T>(parsed_value);
    return true;
}

/** Parse four exact Classical CAN DLC values separated by commas. */
bool parse_tpdo_dlc(const std::string_view text, std::array<std::uint8_t, 4>& values) {
    std::size_t begin = 0U;
    for (std::size_t index = 0U; index < values.size(); ++index) {
        const auto end = text.find(',', begin);
        if ((index + 1U < values.size() && end == std::string_view::npos)
            || (index + 1U == values.size() && end != std::string_view::npos)) {
            return false;
        }
        const auto token = text.substr(begin, end == std::string_view::npos ? end : end - begin);
        if (!parse_unsigned(token, values[index]) || values[index] > 8U) {
            return false;
        }
        begin = end == std::string_view::npos ? text.size() : end + 1U;
    }
    return begin == text.size();
}

/** Parse and validate all startup values before opening SocketCAN. */
Result<Options> parse_options(const int argc, char* argv[]) {
    Options options;
    std::array<bool, 9> seen{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (index + 1 >= argc || std::string_view{argv[index + 1]}.starts_with("--")) {
            return argument_error("missing value argument=" + std::string{argument});
        }
        const std::string_view value{argv[++index]};
        std::size_t slot = seen.size();
        bool valid = true;
        if (argument == "--interface") {
            slot = 0U;
            options.stack.interface_name = value;
        } else if (argument == "--controller-node") {
            slot = 1U;
            valid = parse_unsigned(value, options.stack.controller_node_id);
        } else if (argument == "--remote-node") {
            slot = 2U;
            valid = parse_unsigned(value, options.stack.remote_node_id);
        } else if (argument == "--bitrate-kbit") {
            slot = 3U;
            valid = parse_unsigned(value, options.stack.bit_rate_kbit_s);
        } else if (argument == "--heartbeat-timeout-ms") {
            slot = 4U;
            std::uint16_t milliseconds = 0U;
            valid = parse_unsigned(value, milliseconds);
            options.stack.heartbeat_timeout = std::chrono::milliseconds{milliseconds};
        } else if (argument == "--sdo-timeout-ms") {
            slot = 5U;
            std::uint16_t milliseconds = 0U;
            valid = parse_unsigned(value, milliseconds);
            options.stack.sdo_timeout = std::chrono::milliseconds{milliseconds};
        } else if (argument == "--tpdo-timeout-ms") {
            slot = 6U;
            std::uint16_t milliseconds = 0U;
            valid = parse_unsigned(value, milliseconds);
            options.stack.tpdo_timeout = std::chrono::milliseconds{milliseconds};
        } else if (argument == "--tpdo-dlc") {
            slot = 7U;
            valid = parse_tpdo_dlc(value, options.stack.tpdo_expected_dlc);
        } else if (argument == "--duration-ms") {
            slot = 8U;
            std::uint32_t milliseconds = 0U;
            valid = parse_unsigned(value, milliseconds) && milliseconds > 0U
                    && std::chrono::milliseconds{milliseconds} <= maximum_duration;
            options.duration = std::chrono::milliseconds{milliseconds};
        } else {
            return argument_error("unknown argument=" + std::string{argument});
        }
        if (!valid) {
            return argument_error("invalid value argument=" + std::string{argument});
        }
        if (slot < seen.size()) {
            if (seen[slot]) {
                return argument_error("duplicate argument=" + std::string{argument});
            }
            seen[slot] = true;
        }
    }
    if (std::any_of(seen.begin(), seen.begin() + 8, [](const bool value) {
            return !value;
        })) {
        return argument_error("missing required observer configuration");
    }
    const auto status = validate_stack_config(options.stack);
    return status.ok() ? Result<Options>::success(std::move(options)) : Result<Options>::failure(status);
}

/** Format all eight retained payload bytes as lowercase hexadecimal. */
std::string payload_hex(const RawCanopenFrame& frame) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto value : frame.payload) {
        output << std::setw(2) << static_cast<unsigned int>(value);
    }
    return output.str();
}

/** Print one raw CANopen frame without decoding vendor meaning. */
void print_frame(const std::string_view service, const FrameObservation& observation) {
    if (!observation.present) {
        return;
    }
    const auto& raw = observation.raw;
    const auto timestamp_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(raw.received_at.time_since_epoch()).count();
    std::cout << "event=observation service=" << service << " current=" << observation.current << " raw_can_id=0x"
              << std::hex << std::setfill('0') << std::setw(8) << raw.identifier << std::dec << std::setfill(' ')
              << " dlc=" << static_cast<unsigned int>(raw.dlc) << " payload=" << payload_hex(raw)
              << " timestamp_ns=" << timestamp_ns << " transport=" << raw.generation.transport
              << " boot=" << raw.generation.boot << '\n';
}

/** Print every retained raw service when the immutable snapshot version changes. */
void print_snapshot(const ObservationSnapshot& snapshot) {
    std::cout << "event=snapshot version=" << snapshot.version << " transport=" << snapshot.generation.transport
              << " boot=" << snapshot.generation.boot << " malformed_count=" << snapshot.malformed_count
              << " replay_count=" << snapshot.replay_count << " sdo_rejection_count=" << snapshot.sdo_rejection_count
              << '\n';
    print_frame("boot", snapshot.boot);
    print_frame("nmt", {.present = snapshot.nmt.present, .current = snapshot.nmt.current, .raw = snapshot.nmt.raw});
    print_frame("heartbeat", snapshot.heartbeat.frame);
    print_frame("emcy", snapshot.emergency.frame);
    print_frame("sdo", snapshot.sdo_result.frame);
    print_frame("sdo_rejected", snapshot.sdo_rejected);
    for (std::size_t index = 0U; index < snapshot.tpdo.size(); ++index) {
        print_frame(std::string{"tpdo"} + std::to_string(index + 1U), snapshot.tpdo[index]);
    }
    print_frame("malformed", snapshot.malformed);
    print_frame("can_error", snapshot.can_error);
}

/** Print one context-rich project status. */
void print_error(const Status& status) {
    std::cerr << "event=error operation=" << status.operation << " context=\"" << status.context
              << "\" errno=" << status.error.value() << " message=\"" << status.error.message() << "\"\n";
}

/** Run the normal deny-gated CANopen owner to deadline or synchronous signal. */
int run_observer(const Options& options) {
    auto termination = TerminationEvent::create();
    if (!termination.ok()) {
        print_error(termination.status());
        return 1;
    }
    auto lifecycle = Lifecycle::create(options.stack, termination.value());
    if (!lifecycle.ok()) {
        print_error(lifecycle.status());
        return 1;
    }
    std::cout << "event=start observer_version=1 interface=" << options.stack.interface_name
              << " controller=" << static_cast<unsigned int>(options.stack.controller_node_id)
              << " remote=" << static_cast<unsigned int>(options.stack.remote_node_id)
              << " duration_ms=" << options.duration.count() << '\n';
    const auto deadline = std::chrono::steady_clock::now() + options.duration;
    std::uint64_t printed_version = 0U;
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const auto result = lifecycle.value()->run_until(std::min(deadline, now + observation_step));
        if (!result.ok()) {
            print_error(result.status());
            return 1;
        }
        const auto snapshot = lifecycle.value()->observation_snapshot(std::chrono::steady_clock::now());
        if (snapshot.version != printed_version) {
            printed_version = snapshot.version;
            print_snapshot(snapshot);
        }
        if (result.value() == LifecycleExit::deadline && std::chrono::steady_clock::now() >= deadline) {
            std::cout << "event=summary reason=deadline version=" << printed_version << '\n';
            return 0;
        }
        if (result.value() == LifecycleExit::sigint || result.value() == LifecycleExit::sigterm) {
            const int signal = result.value() == LifecycleExit::sigint ? SIGINT : SIGTERM;
            std::cout << "event=summary reason=signal signal=" << signal << " version=" << printed_version << '\n';
            return 128 + signal;
        }
        if (result.value() != LifecycleExit::deadline) {
            std::cout << "event=summary reason=owner-exit version=" << printed_version << '\n';
            return 1;
        }
    }
}

} // namespace

/** Run one bounded, normal deny-gated CANopen observer process. */
int main(const int argc, char* argv[]) {
    if (argc == 2 && std::string_view{argv[1]} == "--help") {
        print_usage();
        return 0;
    }
    auto options = parse_options(argc, argv);
    if (!options.ok()) {
        print_error(options.status());
        print_usage();
        return 2;
    }
    return run_observer(options.value());
}
