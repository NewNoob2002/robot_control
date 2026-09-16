#include "input/sbus/linux/reader.hpp"
#include "platform/linux/process/termination_event.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace {
using namespace std::chrono_literals;
using robot_control::input::sbus::Reader;
using robot_control::input::sbus::ReaderConfig;
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

/** Observe input for a fixed deadline; output blockage is a terminal error. */
int observe(const std::string& device, const std::chrono::milliseconds duration, const ReaderConfig config) {
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
          << " duration_ms=" << duration.count() << '\n';
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
        if (!result.ok()) {
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
        if (batch.raw_size == 0 && batch.discontinuity == robot_control::input::sbus::Discontinuity::none)
            continue;
        if (batch.raw_size > maximum_capture_bytes - captured_bytes)
            return failure(Status::from_errno("capture byte limit", device, EOVERFLOW));
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
        if (!emit(output.str()))
            return 1;
    }
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
    const std::string usage = "Usage: robot-control-sbus-observer --device <tty> "
                              "[--duration-ms 1..60000] [--parity even|none]\n"
                              "Default: 100000 8E2, 1000 ms; none is explicit diagnostic mode.\n";
    if (argc == 2 && std::string_view{argv[1]} == "--help")
        return emit(usage) ? 0 : 1;
    std::string device;
    std::chrono::milliseconds duration{1000};
    ReaderConfig config;
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
        } else {
            valid = false;
        }
    }
    if (!valid || device.empty()) {
        static_cast<void>(emit(usage));
        return 2;
    }
    return observe(device, duration, config);
}
