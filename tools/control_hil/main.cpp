#include "application/control/linux_loop.hpp"
#include "communication/canopen/control_qualification.hpp"

#include <charconv>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <string_view>
#include <unistd.h>

namespace {
using namespace robot_control;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using platform::linux::Status;

/** Report primary and cleanup results separately; stderr is captured by the trial runner. */
void report(const char* phase, const Status& status) {
    std::cerr << "phase=" << phase << " ok=" << status.ok() << " operation=" << status.operation
              << " context=" << status.context << " errno=" << status.error.value() << '\n';
}

/** Run one bounded zero-only session using the actual SBUS/ControlLoop/runtime path. */
int run(const std::string& interface, std::chrono::milliseconds duration, const std::string& device) {
    auto termination = platform::linux::process::TerminationEvent::create();
    if (!termination.ok()) {
        report("termination", termination.status());
        return 1;
    }
    input::sbus::Reader reader;
    input::sbus::ReaderConfig serial;
    serial.serial.even_parity = interface != "vcan0"; // Explicit namespace PTY fixture only.
    auto status = reader.open(device, serial);
    if (!status.ok()) {
        report("uart", status);
        return 1;
    }
    communication::canopen::StackConfig stack{.interface_name = interface,
                                              .controller_node_id = 127,
                                              .remote_node_id = 1,
                                              .heartbeat_timeout = 1000ms,
                                              .sdo_timeout = 250ms,
                                              .tpdo_timeout = 200ms,
                                              .tpdo_expected_dlc = {8, 5, 0, 0}};
    auto created = communication::canopen::Lifecycle::create(stack, termination.value());
    if (!created.ok()) {
        report("can", created.status());
        return 1;
    }
    auto owner = std::move(created).value();
    communication::canopen::ControlQualification qualification{*owner};
    const auto proof = qualification.prepare();
    status = proof.status();
    application::control::LoopResult result;
    unsigned enabled_samples = 0;
    if (proof.ok()) {
        // The operator-calibrated fixed trial profile is injected at startup.
        input::sbus::SourceConfig profile;
        profile.steering = {200, 1000, 1800, false};
        profile.throttle = {200, 993, 1800, false};
        profile.gear_rpm = {5, 5, 5};
        profile.maximum_rpm = 5;
        input::sbus::Source source{profile};
        application::control::CycleConfig config;
        config.runtime = {
            domain::drive::PackedHalf::low, domain::drive::PackedHalf::low, 1, 1, 5, 0, 1000ms, 200ms, 50ms, 0x8000};
        config.arbiter.max_abs_rpm = 5;
        config.transition_timeout = 1000ms;
        auto attached = application::control::ControlLoop::create(*owner, reader, source, config, proof.value());
        status = attached.status();
        if (attached.ok()) {
            auto loop = std::move(attached).value();
            status = reader.open(device, serial); // Flush preparation backlog and require a fresh receiver session.
            const auto started = Clock::now();
            auto next_log = started;
            std::cout << "event=control_start duration_ms=" << duration.count()
                      << " targets=zero operator=neutral_release_then_press" << '\n'
                      << std::flush;
            while (status.ok() && Clock::now() - started < duration) {
                const auto before = loop->runtime_state();
                result = loop->step({.coherent = true,
                                     .emergency_stop_known = before.healthy,
                                     .emergency_stop_active = (before.feedback.status_raw & 0x8000U) != 0});
                const auto state = loop->runtime_state();
                status = result.status;
                if (result.finished) {
                    if (status.ok())
                        status = Status::from_errno("control_hil_interrupted", interface, ECANCELED);
                    break;
                }
                if (!state.healthy || (enabled_samples > 0 && !state.armed) || state.left_tenths_rpm != 0
                    || state.right_tenths_rpm != 0 || result.source.candidate.left_rpm != 0
                    || result.source.candidate.right_rpm != 0)
                    status = Status::from_errno(
                        "control_hil_zero_envelope",
                        interface + " healthy=" + std::to_string(state.healthy)
                            + " armed=" + std::to_string(state.armed)
                            + " reason=" + std::to_string(static_cast<unsigned>(state.output.reason))
                            + " status=" + std::to_string(state.feedback.status_raw) + " source_fault="
                            + std::to_string(static_cast<unsigned>(result.source.fault)) + " source_age_ms="
                            + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 Clock::now() - result.source.sample.captured_at)
                                                 .count()),
                        EACCES);
                const auto axes = domain::drive::decode_dual_axis_status(state.feedback.status_raw);
                if (state.armed && axes.low_half.state == domain::drive::Cia402State::operation_enabled
                    && axes.high_half.state == domain::drive::Cia402State::operation_enabled)
                    ++enabled_samples;
                if (Clock::now() >= next_log) {
                    next_log = Clock::now() + 100ms;
                    std::cout << "event=control elapsed_ms="
                              << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count()
                              << " source_health=" << static_cast<unsigned>(result.source.health)
                              << " authorization=" << result.source.sample.authorization_generation
                              << " armed=" << state.armed << " status=" << state.feedback.status_raw
                              << " word=" << std::to_integer<unsigned>(state.output.payload[0])
                              << " enabled_samples=" << enabled_samples << '\n'
                              << std::flush;
                    if (!std::cout.good())
                        status = Status::from_errno("control_hil_output", interface, EIO);
                }
            }
            result = loop->stop();
            if (status.ok())
                status = result.status;
            if (status.ok() && enabled_samples < 10)
                status = Status::from_errno("control_hil_no_zero_enable", interface, ENODATA);
            report("runtime_stop", result.stop_status);
            // Detach the sole RPDO writer before any SDO cleanup.
            loop.reset();
        }
    }
    const auto cleanup = qualification.finish();
    report("control", status);
    report("restore", cleanup);
    std::cout << "event=summary cycles=" << result.cycles << " enabled_samples=" << enabled_samples
              << " missed_periods=" << result.missed_periods << " maximum_lateness_us="
              << std::chrono::duration_cast<std::chrono::microseconds>(result.maximum_lateness).count()
              << " maximum_cycle_us="
              << std::chrono::duration_cast<std::chrono::microseconds>(result.maximum_cycle_time).count()
              << " shutdown_us="
              << std::chrono::duration_cast<std::chrono::microseconds>(result.shutdown_elapsed).count()
              << " primary_ok=" << status.ok() << " restore_ok=" << cleanup.ok() << '\n'
              << std::flush;
    return status.ok() && cleanup.ok() && std::cout.good() ? 0 : 1;
}
} // namespace

/** Explicit bounded qualification CLI; no interface creation, motion option or automatic retry. */
int main(int argc, char* argv[]) {
    const std::string_view usage =
        "Usage: robot-control-hil --interface can0 --device /dev/tty... --duration-ms 2000..20000 --zero-only\n";
    if (argc == 2 && std::string_view{argv[1]} == "--help") {
        std::cout << usage;
        return 0;
    }
    if (argc != 8 || std::string_view{argv[1]} != "--interface" || std::string_view{argv[3]} != "--device"
        || std::string_view{argv[5]} != "--duration-ms" || std::string_view{argv[7]} != "--zero-only") {
        std::cerr << usage;
        return 2;
    }
    const std::string interface{argv[2]}, device{argv[4]};
    const std::string_view duration{argv[6]};
    unsigned milliseconds = 0;
    const auto parsed = std::from_chars(duration.data(), duration.data() + duration.size(), milliseconds);
    const auto* fixture = std::getenv("ROBOT_CONTROL_TEST_VCAN_INTERFACE");
    if (parsed.ec != std::errc{} || parsed.ptr != duration.data() + duration.size() || milliseconds < 2000
        || milliseconds > 20000 || !device.starts_with("/dev/")
        || (interface != "can0" && !(interface == "vcan0" && fixture && std::string_view{fixture} == "vcan0"))) {
        std::cerr << usage;
        return 2;
    }
    // Blocking diagnostic output must not hold an enabled drive indefinitely.
    const int flags = ::fcntl(STDOUT_FILENO, F_GETFL);
    const int err_flags = ::fcntl(STDERR_FILENO, F_GETFL);
    if (flags < 0 || err_flags < 0 || ::fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK) < 0
        || ::fcntl(STDERR_FILENO, F_SETFL, err_flags | O_NONBLOCK) < 0 || ::signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        return 1;
    const auto result = run(interface, std::chrono::milliseconds{milliseconds}, device);
    static_cast<void>(::fcntl(STDOUT_FILENO, F_SETFL, flags));
    static_cast<void>(::fcntl(STDERR_FILENO, F_SETFL, err_flags));
    return result;
}
