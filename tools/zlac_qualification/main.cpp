#include "communication/canopen/qualification.hpp"
#include "platform/linux/process/termination_event.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

using namespace std::chrono_literals;
using robot_control::communication::canopen::CommunicationLossStimulus;
using robot_control::communication::canopen::Lifecycle;
using robot_control::communication::canopen::LifecycleExit;
using robot_control::communication::canopen::QualificationNmt;
using robot_control::communication::canopen::QualificationSession;
using robot_control::communication::canopen::StackConfig;
using robot_control::domain::drive::zlac8015d::IndependentChannel;
using robot_control::domain::drive::zlac8015d::TransitionControlword;
using robot_control::platform::linux::Status;
using robot_control::platform::linux::process::TerminationEvent;

enum class Operation : std::uint8_t {
    none,
    nmt,
    velocity_mode,
    controlword,
    zero_targets,
    target_once,
    zero_sequence,
    nmt_stop_once,
    shutdown_once,
    disable_voltage_once,
    quick_stop_once,
    watchdog_once,
    heartbeat_loss_once,
    tpdo_loss_once,
    manual_tpdo
};

struct Arguments {
    std::string interface_name;
    Operation operation{Operation::none};
    QualificationNmt nmt{QualificationNmt::pre_operational};
    TransitionControlword controlword{TransitionControlword::shutdown};
    IndependentChannel channel{IndependentChannel::subindex_1};
    std::int32_t rpm{0};
    std::chrono::milliseconds duration{0};
};

/** Print the fixed qualification syntax without opening CAN. */
void usage() {
    std::cerr << "Usage: robot-control-zlac-qualification --interface IFACE "
                 "(--nmt operational|stopped|pre-operational | --velocity-mode | "
                 "--controlword shutdown|switch-on|enable-operation | --zero-targets | "
                 "--zero-sequence | --target-once SUBINDEX:RPM --duration-ms 1..3000 | "
                 "--nmt-stop-once SUBINDEX:RPM --duration-ms 1..2000 | "
                 "--shutdown-once SUBINDEX:RPM --duration-ms 1..10000 | "
                 "--disable-voltage-once SUBINDEX:RPM --duration-ms 1..10000 | "
                 "--quick-stop-once SUBINDEX:RPM --duration-ms 1..10000 | "
                 "--watchdog-once | --heartbeat-loss-once | --tpdo-loss-once | --manual-tpdo)\n"
                 "Communication trials are fixed at subindex 2, +5 rpm, 200 ms lead; "
                 "watchdog quiet window 1500 ms, feedback-loss observation limit 750 ms.\n";
}

/** Parse one complete integer token. */
bool parse_integer(const std::string_view text, auto& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return !text.empty() && result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

/** Parse one exact bounded target token. */
bool parse_target(const std::string_view text, Arguments& result) {
    const auto separator = text.find(':');
    std::uint8_t channel = 0U;
    if (separator == std::string_view::npos || !parse_integer(text.substr(0U, separator), channel)
        || !parse_integer(text.substr(separator + 1U), result.rpm) || (channel != 1U && channel != 2U)
        || result.rpm == 0 || result.rpm < -10 || result.rpm > 10) {
        return false;
    }
    result.channel = static_cast<IndependentChannel>(channel);
    return true;
}

/** Parse one mutually exclusive qualification operation before lifecycle creation. */
std::optional<Arguments> parse_arguments(const int argc, char** argv) {
    Arguments result;
    bool duration_seen = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--interface" && index + 1 < argc && result.interface_name.empty()) {
            result.interface_name = argv[++index];
        } else if (argument == "--nmt" && index + 1 < argc && result.operation == Operation::none) {
            const std::string_view value{argv[++index]};
            if (value == "operational") {
                result.nmt = QualificationNmt::operational;
            } else if (value == "stopped") {
                result.nmt = QualificationNmt::stopped;
            } else if (value == "pre-operational") {
                result.nmt = QualificationNmt::pre_operational;
            } else {
                return std::nullopt;
            }
            result.operation = Operation::nmt;
        } else if (argument == "--velocity-mode" && result.operation == Operation::none) {
            result.operation = Operation::velocity_mode;
        } else if (argument == "--controlword" && index + 1 < argc && result.operation == Operation::none) {
            const std::string_view value{argv[++index]};
            if (value == "shutdown") {
                result.controlword = TransitionControlword::shutdown;
            } else if (value == "switch-on") {
                result.controlword = TransitionControlword::switch_on;
            } else if (value == "enable-operation") {
                result.controlword = TransitionControlword::enable_operation;
            } else {
                return std::nullopt;
            }
            result.operation = Operation::controlword;
        } else if (argument == "--zero-sequence" && result.operation == Operation::none) {
            result.operation = Operation::zero_sequence;
        } else if (argument == "--zero-targets" && result.operation == Operation::none) {
            result.operation = Operation::zero_targets;
        } else if (argument == "--target-once" && index + 1 < argc && result.operation == Operation::none) {
            ++index;
            if (!parse_target(argv[index], result)) {
                return std::nullopt;
            }
            result.operation = Operation::target_once;
        } else if (argument == "--nmt-stop-once" && index + 1 < argc && result.operation == Operation::none) {
            ++index;
            if (!parse_target(argv[index], result)) {
                return std::nullopt;
            }
            result.operation = Operation::nmt_stop_once;
        } else if (argument == "--shutdown-once" && index + 1 < argc && result.operation == Operation::none) {
            ++index;
            if (!parse_target(argv[index], result)) {
                return std::nullopt;
            }
            result.operation = Operation::shutdown_once;
        } else if (argument == "--disable-voltage-once" && index + 1 < argc && result.operation == Operation::none) {
            ++index;
            if (!parse_target(argv[index], result)) {
                return std::nullopt;
            }
            result.operation = Operation::disable_voltage_once;
        } else if (argument == "--quick-stop-once" && index + 1 < argc && result.operation == Operation::none) {
            ++index;
            if (!parse_target(argv[index], result)) {
                return std::nullopt;
            }
            result.operation = Operation::quick_stop_once;
        } else if (argument == "--watchdog-once" && result.operation == Operation::none) {
            result.operation = Operation::watchdog_once;
        } else if (argument == "--heartbeat-loss-once" && result.operation == Operation::none) {
            result.operation = Operation::heartbeat_loss_once;
        } else if (argument == "--tpdo-loss-once" && result.operation == Operation::none) {
            result.operation = Operation::tpdo_loss_once;
        } else if (argument == "--manual-tpdo" && result.operation == Operation::none) {
            result.operation = Operation::manual_tpdo;
        } else if (argument == "--duration-ms" && index + 1 < argc && !duration_seen) {
            std::int64_t duration = 0;
            if (!parse_integer(std::string_view{argv[++index]}, duration) || duration < 1 || duration > 10000) {
                return std::nullopt;
            }
            result.duration = std::chrono::milliseconds{duration};
            duration_seen = true;
        } else {
            return std::nullopt;
        }
    }
    const bool bounded_motion =
        result.operation == Operation::target_once || result.operation == Operation::nmt_stop_once
        || result.operation == Operation::shutdown_once || result.operation == Operation::disable_voltage_once
        || result.operation == Operation::quick_stop_once;
    if (result.interface_name.empty() || result.operation == Operation::none || bounded_motion != duration_seen
        || (result.operation == Operation::target_once && result.duration > 3000ms)
        || (result.operation == Operation::nmt_stop_once && result.duration > 2000ms)) {
        return std::nullopt;
    }
    return result;
}

/** Print one context-rich qualification failure. */
void report(const Status& status) {
    std::cerr << status.operation << ": " << status.context << ": " << status.error.message() << '\n';
}

/** Build the fixed node-1 qualification configuration. */
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

/** Prefer passive node-1 startup evidence before any bounded operation. */
Status wait_ready(Lifecycle& owner, const std::chrono::milliseconds timeout, const bool heartbeat_required) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto snapshot = owner.observation_snapshot(std::chrono::steady_clock::now());
        if (snapshot.heartbeat.frame.current || (!heartbeat_required && snapshot.boot_observed)) {
            return Status::success();
        }
        const auto run = owner.run_until(std::min(deadline, std::chrono::steady_clock::now() + 10ms));
        if (!run.ok() || run.value() != LifecycleExit::deadline) {
            return run.ok() ? Status::from_errno("qualification_owner_exit", "node=1", ECANCELED) : run.status();
        }
    }
    return Status::from_errno("qualification_ready", "node=1", ETIMEDOUT);
}

} // namespace

/** Parse and execute one bounded Phase 6 qualification operation. */
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
    const bool communication_loss = arguments->operation == Operation::watchdog_once
                                    || arguments->operation == Operation::heartbeat_loss_once
                                    || arguments->operation == Operation::tpdo_loss_once;
    auto configuration = config(arguments->interface_name);
    if (communication_loss) {
        configuration.heartbeat_timeout = 500ms;
    }
    auto owner = Lifecycle::create(configuration, termination.value());
    if (!owner.ok()) {
        report(owner.status());
        return 1;
    }
    const bool online_probe_fallback =
        arguments->operation == Operation::nmt_stop_once || arguments->operation == Operation::shutdown_once
        || arguments->operation == Operation::disable_voltage_once || arguments->operation == Operation::quick_stop_once
        || communication_loss || arguments->operation == Operation::manual_tpdo;
    const auto startup_timeout =
        arguments->operation == Operation::zero_sequence || arguments->operation == Operation::target_once ? 180s : 1s;
    std::cout << "qualification_wait_boot node=1 timeout_s=" << startup_timeout.count() << std::endl;
    const auto ready = wait_ready(*owner.value(), startup_timeout, !online_probe_fallback);
    if (!ready.ok() && !(online_probe_fallback && ready.error.value() == ETIMEDOUT)) {
        report(ready);
        return 1;
    }
    if (!ready.ok()) {
        std::cout << "qualification_online_probe node=1 object=0x1017:00" << std::endl;
    }

    QualificationSession session{*owner.value()};
    Status result = Status::from_errno("qualification_operation", "unselected", EINVAL);
    switch (arguments->operation) {
        case Operation::nmt:
            result = session.send_nmt(arguments->nmt);
            break;
        case Operation::velocity_mode:
            result = session.set_velocity_mode();
            break;
        case Operation::controlword:
            result = session.send_controlword(arguments->controlword);
            break;
        case Operation::zero_targets:
            result = session.set_zero_targets();
            break;
        case Operation::target_once:
            result =
                session.qualify_first_motion_cia402(arguments->channel, arguments->rpm, arguments->duration, 2000ms);
            break;
        case Operation::zero_sequence:
            result = session.qualify_zero_target_cia402(2000ms);
            break;
        case Operation::nmt_stop_once:
            result = session.qualify_nmt_stop_cia402(arguments->channel, arguments->rpm, arguments->duration, 2000ms);
            break;
        case Operation::shutdown_once:
            result = session.qualify_shutdown_cia402(arguments->channel, arguments->rpm, arguments->duration, 2000ms);
            break;
        case Operation::disable_voltage_once:
            result =
                session.qualify_disable_voltage_cia402(arguments->channel, arguments->rpm, arguments->duration, 2000ms);
            break;
        case Operation::quick_stop_once:
            result = session.qualify_quick_stop_cia402(arguments->channel, arguments->rpm, arguments->duration, 2000ms);
            break;
        case Operation::watchdog_once:
            result = session.qualify_communication_loss_cia402(CommunicationLossStimulus::watchdog);
            break;
        case Operation::heartbeat_loss_once:
            result = session.qualify_communication_loss_cia402(CommunicationLossStimulus::heartbeat);
            break;
        case Operation::tpdo_loss_once:
            result = session.qualify_communication_loss_cia402(CommunicationLossStimulus::tpdo);
            break;
        case Operation::manual_tpdo:
            result = session.capture_manual_tpdo(60s);
            break;
        case Operation::none:
            break;
    }
    if (!result.ok()) {
        report(result);
        return 1;
    }
    std::cout << "qualification_complete node=1 operation=" << static_cast<unsigned int>(arguments->operation) << '\n';
    return 0;
}
