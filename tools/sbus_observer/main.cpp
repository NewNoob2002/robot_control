#include "input/sbus/linux/reader.hpp"
#include "input/sbus/linux/source_bridge.hpp"
#include "platform/linux/process/termination_event.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace {
using namespace std::chrono_literals;
using robot_control::input::sbus::AxisConfig;
using robot_control::input::sbus::Reader;
using robot_control::input::sbus::ReaderConfig;
using robot_control::input::sbus::Source;
using robot_control::input::sbus::SourceConfig;
using robot_control::input::sbus::SourceSnapshot;
using robot_control::platform::linux::Status;

/** Restore flags shared with the parent process on every ordinary exit path. */
struct OutputFlags {
    int original;
    /** Restore the borrowed stdout descriptor without closing it. */
    ~OutputFlags() {
        static_cast<void>(::fcntl(STDOUT_FILENO, F_SETFL, original));
    }
};

/** Write one bounded record to nonblocking stdout; fail instead of dropping it. */
bool emit(const std::string& text) {
    return ::write(STDOUT_FILENO, text.data(), text.size()) == static_cast<ssize_t>(text.size());
}

/** Format a contextual error on the same bounded output path. */
int failure(const Status& status) {
    std::ostringstream output;
    output << "event=error operation=" << std::quoted(status.operation) << " device=" << std::quoted(status.context)
           << " errno=" << status.error.value() << '\n';
    static_cast<void>(emit(output.str()));
    return 1;
}

/** Parse four explicit axis integers; never infer calibration or inversion. */
std::optional<AxisConfig> parse_axis(std::string_view text) {
    std::array<unsigned int, 4> values{};
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto comma = text.find(',');
        if ((i + 1 < values.size()) != (comma != std::string_view::npos))
            return std::nullopt;
        const auto token = text.substr(0, comma);
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), values[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || values[i] > (i == 3 ? 1U : 2047U))
            return std::nullopt;
        if (comma != std::string_view::npos)
            text.remove_prefix(comma + 1);
    }
    if (values[0] >= values[1] || values[1] >= values[2])
        return std::nullopt;
    return AxisConfig{static_cast<std::uint16_t>(values[0]), static_cast<std::uint16_t>(values[1]),
                      static_cast<std::uint16_t>(values[2]), values[3] != 0};
}

/** Give diagnostic reasons stable names instead of requiring enum-number knowledge. */
std::string_view fault_name(robot_control::input::sbus::InputFault fault) {
    constexpr std::array names{
        std::string_view{"none"},          std::string_view{"configuration"},    std::string_view{"rejected"},
        std::string_view{"frame_lost"},    std::string_view{"failsafe"},         std::string_view{"timeout"},
        std::string_view{"future_time"},   std::string_view{"replay"},           std::string_view{"transport"},
        std::string_view{"discontinuity"}, std::string_view{"session_changed"},  std::string_view{"counter_exhausted"},
        std::string_view{"malformed"},     std::string_view{"clock_regression"}, std::string_view{"shutdown"}};
    const auto index = static_cast<std::size_t>(fault);
    return index < names.size() ? names[index] : "unknown";
}

/** Format one owned diagnostic snapshot; this tool has no drive-command output. */
std::string source_record(const SourceSnapshot& state) {
    const auto& sample = state.sample;
    std::ostringstream line;
    line << "event=source session=" << sample.session_generation << " sequence=" << sample.sequence << " captured_ns="
         << std::chrono::duration_cast<std::chrono::nanoseconds>(sample.captured_at.time_since_epoch()).count()
         << " authorization=" << sample.authorization_generation << " valid=" << sample.valid
         << " coherent=" << sample.coherent << " enabled=" << sample.enabled << " lost=" << sample.lost
         << " failsafe=" << sample.failsafe << " left_rpm=" << sample.command.left_rpm
         << " right_rpm=" << sample.command.right_rpm << " stop=" << sample.command.stop_requested
         << " health=" << static_cast<unsigned int>(state.health) << " recovery=" << state.recovery_count
         << " fault=" << fault_name(state.fault) << " last_fault=" << fault_name(state.last_fault)
         << " last_fault_flags=" << static_cast<unsigned int>(state.last_fault_flags) << " steering=" << state.steering
         << " throttle=" << state.throttle << " candidate_left=" << state.candidate.left_rpm
         << " candidate_right=" << state.candidate.right_rpm << '\n';
    return line.str();
}

/** Observe input for a fixed deadline; output blockage is a terminal error. */
int observe(const std::string& device, const std::chrono::milliseconds duration, const ReaderConfig config,
            const std::optional<SourceConfig>& profile) {
    std::optional<Source> source;
    if (profile)
        source.emplace(*profile);
    auto termination = robot_control::platform::linux::process::TerminationEvent::create();
    if (!termination.ok())
        return failure(termination.status());
    Reader reader;
    auto opened = reader.open(device, config);
    if (!opened.ok())
        return failure(opened);
    auto actual = reader.configuration();
    if (!actual.ok())
        return failure(actual.status());
    std::ostringstream start;
    start << "event=start device=" << std::quoted(device)
          << " baud=" << static_cast<unsigned int>(actual.value().baud_rate)
          << " data_bits=8 parity=" << (actual.value().even_parity ? "even" : "none")
          << " stop_bits=" << (actual.value().two_stop_bits ? 2 : 1) << " parmrk=" << actual.value().mark_errors
          << " duration_ms=" << duration.count();
    if (profile) {
        start << " source_policy=compatibility_observation_only steering_axis=" << profile->steering.minimum << ','
              << profile->steering.center << ',' << profile->steering.maximum << ',' << profile->steering.reversed
              << " throttle_axis=" << profile->throttle.minimum << ',' << profile->throttle.center << ','
              << profile->throttle.maximum << ',' << profile->throttle.reversed
              << " channels=" << static_cast<unsigned int>(profile->channels[0]) << ','
              << static_cast<unsigned int>(profile->channels[1]) << ','
              << static_cast<unsigned int>(profile->channels[2]) << ','
              << static_cast<unsigned int>(profile->channels[3]) << " input_deadband=" << profile->deadband
              << " button_thresholds=" << profile->button_release << ',' << profile->button_press
              << " gear_thresholds=" << profile->gear_low << ',' << profile->gear_high
              << " gear_rpm=" << profile->gear_rpm[0] << ',' << profile->gear_rpm[1] << ',' << profile->gear_rpm[2]
              << " maximum_rpm=" << profile->maximum_rpm << " output_deadband_rpm=" << profile->output_deadband_rpm
              << " recovery_frames=" << profile->recovery_frames
              << " timeout_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(profile->timeout).count()
              << " cooldown_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(profile->button_cooldown).count();
    }
    start << '\n';
    if (!emit(start.str()))
        return 1;
    const auto deadline = std::chrono::steady_clock::now() + duration;
    // Bound input volume independently of read fragmentation. One MiB covers
    // 60 s at 100000 8E2 even when PARMRK doubles every received byte.
    constexpr std::size_t maximum_capture_bytes = 1'048'576;
    std::size_t captured_bytes = 0;
    std::size_t frames = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining =
            std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        auto result = reader.read(std::clamp(remaining, 0ms, 20ms), termination.value().fd());
        std::optional<SourceSnapshot> snapshot;
        if (source) {
            const auto now = std::chrono::steady_clock::now();
            snapshot = !result.ok() && result.status().error.value() == ECANCELED
                           ? source->stop(now)
                           : robot_control::input::sbus::update_source(*source, result, now);
        }
        if (!result.ok()) {
            if (snapshot && !emit(source_record(*snapshot)))
                return 1;
            if (result.status().error.value() != ECANCELED)
                return failure(result.status());
            auto signal = termination.value().consume();
            if (!signal.ok())
                return failure(signal.status());
            std::ostringstream end;
            end << "event=summary reason=signal signal=" << signal.value() << " frames=" << frames << '\n';
            if (!emit(end.str()))
                return 1;
            return 128 + signal.value();
        }
        const auto& batch = result.value();
        if (batch.raw_size == 0 && batch.discontinuity == robot_control::input::sbus::Discontinuity::none) {
            if (snapshot && !emit(source_record(*snapshot)))
                return 1;
            continue;
        }
        if (batch.raw_size > maximum_capture_bytes - captured_bytes) {
            if (source)
                static_cast<void>(emit(source_record(source->stop(std::chrono::steady_clock::now()))));
            return failure(Status::from_errno("capture byte limit", device, EOVERFLOW));
        }
        captured_bytes += batch.raw_size;
        std::ostringstream output;
        output << "event=read session=" << batch.session << " received_ns="
               << std::chrono::duration_cast<std::chrono::nanoseconds>(batch.captured_at.time_since_epoch()).count()
               << " discontinuity=" << static_cast<int>(batch.discontinuity) << " kernel_raw=" << std::hex
               << std::setfill('0');
        for (std::size_t index = 0; index < batch.raw_size; ++index)
            output << std::setw(2) << std::to_integer<unsigned int>(batch.raw[index]);
        output << std::dec << '\n';
        for (std::size_t index = 0; index < batch.event_count; ++index) {
            const auto& event = batch.events[index];
            if (event.kind == robot_control::input::sbus::protocol::EventKind::rejected) {
                output << "event=rejected session=" << batch.session << '\n';
                continue;
            }
            output << "event=frame session=" << batch.session << " sequence=" << ++frames
                   << " flags=" << static_cast<unsigned int>(event.frame.raw_flags) << " channels=";
            for (const auto channel : event.frame.channels)
                output << channel << ',';
            output << '\n';
        }
        if (snapshot)
            output << source_record(*snapshot);
        if (!emit(output.str()))
            return 1;
    }
    if (source && !emit(source_record(source->stop(std::chrono::steady_clock::now()))))
        return 1;
    std::ostringstream end;
    end << "event=summary reason=deadline frames=" << frames << '\n';
    return emit(end.str()) ? 0 : 1;
}
} // namespace

/**
 * Run the receive-only SBUS observer; no CAN dependency or automatic reconnect.
 * @param argc Number of arguments.
 * @param argv Borrowed argument vector for this invocation.
 * @return 0 for help/deadline, 2 for usage, 1 for runtime/capture failure,
 * or 128+signal for cancellation. Single-threaded; owns its UART and signalfd.
 */
int main(const int argc, char* argv[]) {
    const int flags = ::fcntl(STDOUT_FILENO, F_GETFL);
    if (flags < 0 || ::fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK) < 0)
        return 1;
    const OutputFlags restore_flags{flags};
    // A closed capture pipe must fail explicitly instead of terminating by SIGPIPE.
    if (::signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        return 1;
    const std::string usage =
        "Usage: robot-control-sbus-observer --device <tty> "
        "[--duration-ms 1..60000] [--parity even|none]\n"
        "Optional pair: --steering-axis min,center,max,reverse --throttle-axis min,center,max,reverse\n"
        "reverse=0|1; prints Source diagnostics with compatibility policy only; no drive output.\n"
        "Default: 100000 8E2, 1000 ms; none is explicit diagnostic mode.\n";
    if (argc == 2 && std::string_view{argv[1]} == "--help")
        return emit(usage) ? 0 : 1;
    std::string device;
    std::chrono::milliseconds duration{1000};
    ReaderConfig config;
    std::optional<AxisConfig> steering;
    std::optional<AxisConfig> throttle;
    bool duration_seen = false;
    bool parity_seen = false;
    bool valid = true;
    for (int index = 1; index < argc && valid; index += 2) {
        if (index + 1 >= argc) {
            valid = false;
            break;
        }
        const std::string_view option{argv[index]};
        const std::string_view value{argv[index + 1]};
        if (option == "--device" && device.empty() && !value.empty() && !value.starts_with("--")) {
            device = value;
        } else if (option == "--duration-ms" && !duration_seen) {
            unsigned int count = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), count);
            valid =
                parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && count > 0 && count <= 60000;
            duration = std::chrono::milliseconds{count};
            duration_seen = true;
        } else if (option == "--parity" && !parity_seen && (value == "even" || value == "none")) {
            config.serial.even_parity = value == "even";
            parity_seen = true;
        } else if (option == "--steering-axis" && !steering) {
            steering = parse_axis(value);
            valid = steering.has_value();
        } else if (option == "--throttle-axis" && !throttle) {
            throttle = parse_axis(value);
            valid = throttle.has_value();
        } else {
            valid = false;
        }
    }
    std::optional<SourceConfig> profile;
    if (steering && throttle) {
        profile.emplace();
        profile->steering = *steering;
        profile->throttle = *throttle;
        valid =
            valid && robot_control::input::sbus::validate(*profile) == robot_control::input::sbus::ConfigError::none;
    }
    if (!valid || device.empty() || steering.has_value() != throttle.has_value()) {
        static_cast<void>(emit(usage));
        return 2;
    }
    return observe(device, duration, config, profile);
}
