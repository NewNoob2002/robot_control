#include "communication/canopen/commissioning.hpp"
#include "communication/canopen/commissioning_gate.h"
#include "platform/linux/process/termination_event.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace std::chrono_literals;
using robot_control::communication::canopen::CommissioningNmt;
using robot_control::communication::canopen::CommissioningSession;
using robot_control::communication::canopen::Lifecycle;
using robot_control::communication::canopen::LifecycleExit;
using robot_control::communication::canopen::SdoOutcome;
using robot_control::communication::canopen::StackConfig;
using robot_control::platform::linux::Status;
using robot_control::platform::linux::process::TerminationEvent;

struct Arguments {
    std::string interface_name;
    std::optional<CommissioningNmt> nmt;
    std::optional<std::pair<std::uint16_t, std::uint8_t>> upload;
    bool retry_once{false};
};

/** Print the fixed commissioning command syntax without opening CAN. */
void usage() {
    std::cerr << "Usage: robot-control-canopen-commission --interface IFACE "
                 "(--nmt stopped|pre-operational | --upload INDEX:SUBINDEX [--retry-once])\n";
}

/** Parse one complete hexadecimal token into the supplied unsigned value. */
bool parse_hex(const std::string_view text, auto& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    return !text.empty() && result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

/** Parse and whitelist one operation before lifecycle creation. */
std::optional<Arguments> parse_arguments(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--interface" && index + 1 < argc && result.interface_name.empty()) {
            result.interface_name = argv[++index];
        } else if (argument == "--nmt" && index + 1 < argc && !result.nmt && !result.upload) {
            const std::string_view value{argv[++index]};
            if (value == "stopped") {
                result.nmt = CommissioningNmt::stopped;
            } else if (value == "pre-operational") {
                result.nmt = CommissioningNmt::pre_operational;
            } else {
                return std::nullopt;
            }
        } else if (argument == "--upload" && index + 1 < argc && !result.nmt && !result.upload) {
            const std::string_view value{argv[++index]};
            const auto separator = value.find(':');
            std::uint16_t object_index = 0U;
            std::uint8_t subindex = 0U;
            if (separator == std::string_view::npos || !parse_hex(value.substr(0U, separator), object_index)
                || !parse_hex(value.substr(separator + 1U), subindex)
                || robot_control_canopen_sdo_upload_size({.index = object_index, .subindex = subindex}) == 0U) {
                return std::nullopt;
            }
            result.upload = std::pair{object_index, subindex};
        } else if (argument == "--retry-once" && !result.retry_once) {
            result.retry_once = true;
        } else {
            return std::nullopt;
        }
    }
    if (result.interface_name.empty() || (result.nmt.has_value() == result.upload.has_value())
        || (result.retry_once && !result.upload)) {
        return std::nullopt;
    }
    return result;
}

/** Print one context-rich commissioning failure to standard error. */
void report(const Status& status) {
    std::cerr << status.operation << ": " << status.context << ": " << status.error.message() << '\n';
}

/** Build the fixed node-1 commissioning observation configuration. */
StackConfig config(const std::string& interface_name) {
    return {.interface_name = interface_name,
            .controller_node_id = 127U,
            .remote_node_id = 1U,
            .bit_rate_kbit_s = 500U,
            .heartbeat_timeout = 1000ms,
            .sdo_timeout = 500ms,
            .tpdo_timeout = 100ms,
            .tpdo_expected_dlc = {8U, 0U, 0U, 0U}};
}

} // namespace

/** Parse one commissioning operation, execute it once, and report its result. */
int main(const int argc, char** argv) {
    const auto arguments = parse_arguments(argc, argv);
    if (!arguments) {
        usage();
        return 2;
    }
    auto termination = TerminationEvent::create();
    if (!termination.ok()) {
        report(termination.status());
        return 1;
    }
    auto owner = Lifecycle::create(config(arguments->interface_name), termination.value());
    if (!owner.ok()) {
        report(owner.status());
        return 1;
    }

    const auto ready_deadline = std::chrono::steady_clock::now() + 1000ms;
    while (std::chrono::steady_clock::now() < ready_deadline) {
        const auto snapshot = owner.value()->observation_snapshot(std::chrono::steady_clock::now());
        if (snapshot.boot_observed && snapshot.heartbeat.frame.current) {
            break;
        }
        const auto run = owner.value()->run_until(std::min(ready_deadline, std::chrono::steady_clock::now() + 10ms));
        if (!run.ok() || run.value() != LifecycleExit::deadline) {
            if (!run.ok()) {
                report(run.status());
            }
            return 1;
        }
    }

    CommissioningSession session{*owner.value()};
    if (arguments->nmt) {
        const auto status = session.send_nmt(*arguments->nmt);
        if (!status.ok()) {
            report(status);
            return 1;
        }
        std::cout << "nmt_sent node=1 command=0x" << std::hex << static_cast<unsigned int>(*arguments->nmt) << '\n';
        return 0;
    }

    const auto result = session.upload(arguments->upload->first, arguments->upload->second, arguments->retry_once);
    if (!result.ok()) {
        report(result.status());
        return 1;
    }
    const auto& value = result.value();
    std::cout << "sdo node=1 index=0x" << std::hex << std::setw(4) << std::setfill('0') << value.index << " sub=0x"
              << std::setw(2) << static_cast<unsigned int>(value.subindex);
    if (value.outcome == SdoOutcome::abort) {
        std::cout << " abort=0x" << std::setw(8) << value.abort_code << '\n';
        return 1;
    }
    std::cout << " data=";
    for (std::uint8_t index = 0U; index < value.data_length; ++index) {
        std::cout << std::setw(2) << static_cast<unsigned int>(value.data[index]);
    }
    std::cout << '\n';
    return 0;
}
