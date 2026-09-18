#include "service/logging/logger.hpp"
#include "tools/control_hil/trace.hpp"

#include <cerrno>
#include <csignal>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <string_view>
#include <unistd.h>

namespace {
using robot_control::hil::Trace;
using robot_control::service::logging::Logger;
using robot_control::service::logging::Severity;

/** Emit a small key-value record; each record stays below EasyLogger's512-byte line bound. */
bool emit(Logger& logger, std::string_view event, const Trace::Packet& row, std::string_view names) {
    std::istringstream keys{std::string{names}};
    std::ostringstream values;
    values << "ordinal=" << row[0] << " monotonic_ns=" << row[2];
    std::string key;
    std::size_t index = 3;
    while (keys >> key) {
        if (index >= row.size())
            return false;
        values << ' ' << key << '=' << row[index++];
    }
    if (values.str().size() > 390)
        return false;
    logger.log(Severity::info, "control", event, values.str());
    return logger.health().output_failures == 0;
}

/** Consume ordered fixed-width little-endian records; retain bytes before publishing summaries. */
int collect() {
    Logger logger;
    Trace::Packet packet{};
    std::int64_t ordinal = 0;
    bool ended = false;
    std::uint64_t frames = 0, rejected = 0, discontinuities = 0, tx = 0, rx = 0;
    Trace::Packet last_cycle{}, last_frame{};
    while (true) {
        std::size_t offset = 0;
        while (offset < sizeof(packet)) {
            pollfd descriptor{STDIN_FILENO, POLLIN, 0};
            const int ready = ::poll(&descriptor, 1, 15000);
            if (ready < 0 && errno == EINTR)
                continue;
            if (ready <= 0)
                return 1;
            const auto size =
                ::read(STDIN_FILENO, reinterpret_cast<char*>(packet.data()) + offset, sizeof(packet) - offset);
            if (size < 0 && errno == EINTR)
                continue;
            if (size == 0)
                return offset == 0 && ended ? 0 : 1;
            if (size < 0)
                return 1;
            offset += static_cast<std::size_t>(size);
        }
        if (ended || packet[0] != ordinal++ || packet[2] < 0 || packet[1] < 0 || packet[1] > 10
            || (packet[0] == 0 ? packet[1] != 10 || packet[3] != 1 || packet[4] < 0 || packet[4] > 20
                               : packet[1] == 10))
            return 1;
        std::size_t written = 0;
        while (written < sizeof(packet)) {
            const auto count = ::write(STDOUT_FILENO, reinterpret_cast<const char*>(packet.data()) + written,
                                       sizeof(packet) - written);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                return 1;
            written += static_cast<std::size_t>(count);
        }
        switch (static_cast<Trace::Kind>(packet[1])) {
            case Trace::Kind::batch:
                discontinuities += packet[5] != 0;
                break;
            case Trace::Kind::frame:
                ++frames;
                last_frame = packet;
                break;
            case Trace::Kind::rejected:
                ++rejected;
                break;
            case Trace::Kind::can_rx:
                ++rx;
                break;
            case Trace::Kind::can_tx:
                ++tx;
                break;
            case Trace::Kind::cycle:
                last_cycle = packet;
                break;
            case Trace::Kind::timing:
                if (!emit(logger, "control_timing", packet,
                          "cycles missed late_us cycle_us elapsed_ms duration_ms shutdown_us finished primary_errno "
                          "cleanup_errno")
                    || !emit(logger, "input_control", last_cycle,
                             "cycles seq auth session captured_ns steer throttle cand_l cand_r selected_l selected_r "
                             "approved_l approved_r action enabled health")
                    || !emit(logger, "sbus_raw", last_frame, "batch session event ch1 ch3 ch6 ch7 flags kind"))
                    return 1;
                logger.log(Severity::info, "control", "capture_counts",
                           "frames=" + std::to_string(frames) + " rejected=" + std::to_string(rejected)
                               + " discontinuities=" + std::to_string(discontinuities) + " tx=" + std::to_string(tx)
                               + " rx=" + std::to_string(rx));
                break;
            case Trace::Kind::state:
                if (!emit(logger, "drive_state", packet,
                          "source_fault reason status fault_raw mode left right src_age_us hb_age_us tpdo_age_us "
                          "diag_age_us armed healthy epoch auth finished"))
                    return 1;
                break;
            case Trace::Kind::header:
                if (!emit(logger, "diagnostics_start", packet, "version standstill_tenths"))
                    return 1;
                break;
            case Trace::Kind::end:
                if (packet[3] || packet[4]
                    || !emit(logger, "diagnostics_end", packet, "overflow feedback_bad standstill_tenths"))
                    return 1;
                ended = true;
                break;
            case Trace::Kind::stop:
                if (!emit(logger, "control_stop", packet, "cause lifecycle_exit source_seq source_ns"))
                    return 1;
                break;
        }
        if (logger.health().output_failures)
            return 1;
    }
}
} // namespace

/** Separate diagnostic consumer; stdin is the owner pipe, stdout raw evidence, stderr EasyLogger. */
int main(int argc, char* argv[]) {
    if (argc == 2 && std::string_view{argv[1]} == "--help") {
        std::cout << "robot-control-diagnostics: stdin=trace pipe stdout=binary evidence stderr=EasyLogger\n";
        return 0;
    }
    if (argc != 1 || ::signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        return 2;
    try {
        return collect();
    } catch (...) {
        return 1;
    }
}
