#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <net/if.h>
#include <net/if_arp.h>
#include <optional>
#include <stdexcept>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <vector>
#include "app/interface_lock.hpp"
#include "app/io_gate.hpp"
#include "app/runtime_log.hpp"
#include "application/control/linux_loop.hpp"
#include "application/ipc/server.hpp"
#include "communication/canopen/control_qualification.hpp"

namespace {
namespace rc = robot_control;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
using Status = rc::platform::linux::Status;
using Loop = rc::application::control::ControlLoop;
using LifecycleExit = rc::communication::canopen::LifecycleExit;

/** Stable stop-reason values shared by text diagnostics and binary stop records. */
enum class ExitReason : int {
    duration = 0,
    signal = 1,
    input = 2,
    feedback = 3,
    logging = 4,
    runtime = 5,
    preparation = 6
};

/** Borrowed immutable values and runtime accessor from one completed step; owner-local only. */
struct CycleView {
    const rc::application::control::LoopResult& result;
    const rc::input::sbus::ReadBatch& batch;
    const Loop& loop;
};

/** Owned read-only runtime observation and original IPC timing boundaries from one fan-out. */
struct CycleObservation {
    rc::domain::drive::RuntimeState state;
    Clock::time_point ipc_started{}, ipc_finished{};
};

/** Owner-local work statistics, excluding scheduled CAN event waiting and finished cycles. */
struct OwnerMetrics {
    Clock::duration maximum_owner_work{}, maximum_ipc_poll{};
    std::uint64_t measured_owner_cycles{0}, owner_overruns{0};

    /** Accumulate one completed fan-out using its original timing boundaries and configured period. */
    void record(const rc::application::control::LoopResult& result, const CycleObservation& observation,
                Clock::duration period) noexcept {
        if (!result.finished) {
            // Include batch/cycle recording and IPC, excluding scheduled event waiting.
            const auto owner_work = observation.ipc_finished - result.cycle_started_at;
            maximum_owner_work = std::max(maximum_owner_work, owner_work);
            maximum_ipc_poll = std::max(maximum_ipc_poll, observation.ipc_finished - observation.ipc_started);
            ++measured_owner_cycles;
            if (owner_work >= period)
                ++owner_overruns;
        }
    }
};

/** Forward terminal hangup to the blocked SIGTERM path using only async-signal-safe calls. */
void hangup(int) noexcept {
    const int saved = errno;
    static_cast<void>(::kill(::getpid(), SIGTERM));
    errno = saved;
}

/** Print low-rate lifecycle status only; stdout is a nonblocking collector pipe while devices are open. */
void report(const char* phase, const Status& status) {
    std::cout << "event=" << phase << " ok=" << status.ok() << " operation=" << status.operation
              << " context=" << status.context << " errno=" << status.error.value() << '\n'
              << std::flush;
}
/** Publish immutable cycle values; all formatting/disk I/O is done in the logger child. */
void record_cycle(rc::app::RecordWriter& writer, const rc::application::control::LoopResult& result,
                  const rc::domain::drive::RuntimeState& state, const rc::app::MotionMonitor& monitor) noexcept {
    const auto now = Clock::now();
    const auto age = [now](Clock::time_point stamp) -> std::int64_t {
        return stamp == Clock::time_point{} || stamp > now
                   ? -1
                   : std::chrono::duration_cast<std::chrono::microseconds>(now - stamp).count();
    };
    writer.append(rc::app::RecordKind::cycle,
                  {static_cast<std::int64_t>(result.cycles),
                   static_cast<std::int64_t>(result.source.sample.sequence),
                   static_cast<std::int64_t>(result.source.sample.authorization_generation),
                   static_cast<std::int64_t>(result.source.sample.session_generation),
                   result.source.sample.enabled,
                   static_cast<std::int64_t>(result.source.fault),
                   result.control.output.command.left_rpm,
                   result.control.output.command.right_rpm,
                   state.left_tenths_rpm,
                   state.right_tenths_rpm,
                   state.feedback.status_raw,
                   state.feedback.fault_raw,
                   state.feedback.mode_raw,
                   static_cast<std::int64_t>(result.source.health),
                   result.source.candidate.left_rpm,
                   state.armed,
                   age(result.source.sample.captured_at),
                   age(state.feedback.heartbeat_at),
                   age(state.feedback.status_at),
                   static_cast<std::int64_t>(result.missed_periods),
                   std::chrono::duration_cast<std::chrono::microseconds>(result.maximum_lateness).count(),
                   std::chrono::duration_cast<std::chrono::microseconds>(result.maximum_cycle_time).count(),
                   static_cast<std::int64_t>(monitor.transients()),
                   static_cast<std::int64_t>(monitor.fault()),
                   result.source.candidate.right_rpm,
                   static_cast<std::int64_t>(result.control.selected.source),
                   static_cast<std::int64_t>(result.control.request.authorization),
                   result.status.error.value()},
                  now);
}
/** Retain each ordered parser event and all16 raw channels, including rejected candidates. */
void record_batch(rc::app::RecordWriter& writer, const rc::input::sbus::ReadBatch& batch) noexcept {
    if (!batch.session)
        return;
    writer.append(rc::app::RecordKind::batch,
                  {static_cast<std::int64_t>(batch.session), static_cast<std::int64_t>(batch.discontinuity),
                   static_cast<std::int64_t>(batch.raw_size), static_cast<std::int64_t>(batch.event_count)},
                  batch.captured_at);
    for (std::size_t i = 0; i < batch.event_count; ++i) {
        const auto& event = batch.events[i];
        std::array<std::int64_t, 28> fields{static_cast<std::int64_t>(batch.session), static_cast<std::int64_t>(i),
                                            static_cast<std::int64_t>(event.kind), event.frame.raw_flags};
        for (std::size_t channel = 0; channel < 16; ++channel)
            fields[channel + 4] = event.frame.channels[channel];
        writer.append(event.kind == rc::input::sbus::protocol::EventKind::frame ? rc::app::RecordKind::frame
                                                                                : rc::app::RecordKind::rejected,
                      fields, batch.captured_at);
    }
}
/** Observe only after publication, in original order; borrowed values never populate CycleInput.external. */
CycleObservation observe_cycle(const CycleView& cycle, rc::app::RecordWriter& writer, rc::app::MotionMonitor& monitor,
                               rc::application::ipc::Server& ipc) noexcept {
    record_batch(writer, cycle.batch);
    const auto state = cycle.loop.runtime_state();
    static_cast<void>(monitor.ok(Clock::now()));
    record_cycle(writer, cycle.result, state, monitor);
    // Read-only observations after publication; IPC cannot populate CycleInput.external.
    const auto ipc_started = Clock::now();
    ipc.poll({cycle.result.cycles, static_cast<unsigned>(cycle.result.control.selected.source),
              cycle.result.control.output.command.left_rpm, cycle.result.control.output.command.right_rpm,
              state.left_tenths_rpm, state.right_tenths_rpm, state.feedback.status_raw, state.feedback.fault_raw,
              state.healthy, state.armed});
    const auto ipc_finished = Clock::now();
    return {state, ipc_started, ipc_finished};
}

/** Stop after intake is closed, verify within the original one-second window, then detach before restoration. */
Clock::duration stop_and_verify(std::unique_ptr<Loop>& loop, rc::communication::canopen::Lifecycle& owner,
                                rc::app::MotionMonitor& monitor, Status& status, const std::string& interface) {
    const auto stopped_at = Clock::now();
    if (loop) {
        const auto stopped = loop->stop();
        if (status.ok() && !stopped.stop_status.ok())
            status = stopped.stop_status;
        monitor.shutdown(Clock::now());
        while (!monitor.settled(Clock::now()) && Clock::now() - stopped_at < 1s) {
            const auto progress = owner.run_until(Clock::now() + 1ms);
            if (!progress.ok() || progress.value() != LifecycleExit::deadline)
                break;
        }
        if (!monitor.settled(Clock::now()) && status.ok())
            status = Status::from_errno("stop_not_verified_within_1s", interface, ETIMEDOUT);
        loop.reset(); // Detach before any SDO/NMT restoration, including failures.
    }
    return Clock::now() - stopped_at;
}

/** Pure synchronous stop veto after newly received events; cannot create authority or perform I/O. */
bool owner_veto(const void* value, Clock::time_point now) noexcept {
    const auto& io = *static_cast<const rc::app::IoContext*>(value);
    return io.closed || !io.records.good() || !io.monitor.safe(now);
}

/** Run one explicitly configured SBUS owner with exact online setup and bounded stop/restoration. */
int run(const rc::application::config::Startup& config, rc::app::LogSession& logs,
        rc::platform::linux::process::TerminationEvent& termination) {
    auto lock = rc::app::check_interface(config.interface);
    rc::application::ipc::Server ipc{config.ipc_socket, config.cycle.runtime.max_abs_rpm};
    rc::app::IoContext io{
        logs.records(), {}, config.cycle.runtime.max_abs_rpm, false, config.cycle.runtime.speed_resolution};
    rc::app::IoBinding binding{io};
    rc::input::sbus::Reader reader;
    rc::input::sbus::ReaderConfig serial;
    const auto* fixture = std::getenv("ROBOT_CONTROL_TEST_VCAN_INTERFACE");
    const bool virtual_test = fixture && config.interface == fixture && config.interface.starts_with("vcan")
                              && config.device.starts_with("/dev/pts/");
    if (virtual_test)
        serial.serial.even_parity = false;
    auto status = reader.open(config.device, serial);
    report("uart_open", status);
    if (!status.ok())
        return 1;
    rc::communication::canopen::StackConfig stack{.interface_name = config.interface,
                                                  .controller_node_id = 127,
                                                  .remote_node_id = 1,
                                                  .heartbeat_timeout = 1000ms,
                                                  .sdo_timeout = 250ms,
                                                  .tpdo_timeout = 200ms,
                                                  .tpdo_expected_dlc = {8, 5, 0, 0}};
    auto created = rc::communication::canopen::Lifecycle::create(stack, termination);
    report("can_open", created.status());
    if (!created.ok())
        return 1;
    auto owner = std::move(created).value();
    rc::communication::canopen::ControlQualification setup{*owner};
    rc::input::sbus::Source source{config.sbus};
    std::unique_ptr<Loop> loop;
    rc::application::control::LoopResult result;
    int signal_number = 0;
    auto reason = ExitReason::preparation;
    bool prepared = false;
    OwnerMetrics metrics;
    try {
        const auto proof = setup.prepare(true, 20, config.cycle.runtime.speed_resolution, &config.drive);
        status = proof.status();
        report("drive_prepare", status);
        if (proof.ok()) {
            prepared = true;
            io.monitor.start(Clock::now());
            auto attached = Loop::create(*owner, reader, source, config.cycle, proof.value());
            status = attached.status();
            if (attached.ok()) {
                loop = std::move(attached).value();
                status = reader.open(config.device, serial); // Flush setup backlog; require new neutral/CH6.
                const auto started = Clock::now();
                std::cout << "event=control_start interface=" << config.interface << " device=" << config.device
                          << " duration_ms=" << config.run_duration.count()
                          << " source=sbus operator=neutral_release_then_press\n"
                          << std::flush;
                reason = ExitReason::duration;
                while (status.ok() && (config.run_duration == 0ms || Clock::now() - started < config.run_duration)) {
                    if (!logs.healthy()) {
                        reason = ExitReason::logging;
                        status = Status::from_errno("logger_unhealthy", logs.directory(), EIO);
                        break;
                    }
                    const auto before = loop->runtime_state();
                    rc::input::sbus::ReadBatch batch;
                    result = loop->step({.coherent = true,
                                         .emergency_stop_known = before.healthy,
                                         .emergency_stop_active = (before.feedback.status_raw & 0x8000U) != 0},
                                        &batch, owner_veto, &io);
                    const auto observation = observe_cycle({result, batch, *loop}, io.records, io.monitor, ipc);
                    metrics.record(result, observation, config.cycle.period);
                    status = result.status;
                    if (result.finished) {
                        signal_number = result.exit == LifecycleExit::sigint    ? SIGINT
                                        : result.exit == LifecycleExit::sigterm ? SIGTERM
                                                                                : 0;
                        reason = signal_number ? ExitReason::signal : ExitReason::runtime;
                        break;
                    }
                    if (!observation.state.healthy || !io.monitor.safe(Clock::now())) {
                        reason = ExitReason::feedback;
                        status = Status::from_errno("feedback_protection", config.interface, EACCES);
                        break;
                    }
                    // Source loss revokes motion in the control owner while the healthy drive holds zero.
                }
            }
        }
    } catch (const std::exception& error) {
        status = Status::from_errno("app_exception", error.what(), EIO);
        reason = ExitReason::runtime;
    }
    io.closed = true; // New nonzero writes are permanently forbidden before any cleanup operation.
    const auto stop_verification = stop_and_verify(loop, *owner, io.monitor, status, config.interface);
    const auto cleanup = setup.finish();
    report("app_primary", status);
    report("app_cleanup", cleanup);
    std::cout << "event=app_stop reason=" << static_cast<int>(reason) << " signal=" << signal_number
              << " prepared=" << prepared
              << " stop_verify_us=" << std::chrono::duration_cast<std::chrono::microseconds>(stop_verification).count()
              << " transient_samples=" << io.monitor.transients() << " owner_cycles=" << metrics.measured_owner_cycles
              << " owner_max_work_us="
              << std::chrono::duration_cast<std::chrono::microseconds>(metrics.maximum_owner_work).count()
              << " ipc_max_poll_us="
              << std::chrono::duration_cast<std::chrono::microseconds>(metrics.maximum_ipc_poll).count()
              << " owner_overruns=" << metrics.owner_overruns
              << " monitor_fault=" << static_cast<int>(io.monitor.fault()) << '\n'
              << std::flush;
    io.records.append(rc::app::RecordKind::stop,
                      {static_cast<int>(reason), status.error.value(), cleanup.error.value(), signal_number});
    return status.ok() && cleanup.ok() ? (signal_number ? 128 + signal_number : 0) : 1;
}

/** Parse startup arguments before activation; return an early exit code or fill the borrowed configuration. */
std::optional<int> parse_command_line(int argc, char* argv[], rc::application::config::Startup& config) {
    constexpr std::string_view usage =
        "Usage: robot-control-app --config FILE [--interface can0] [--device /dev/ttyACM0]\n"
        "  [--log-dir DIR] [--duration-ms MS] [--set key=value] [--check-config]\n"
        "SBUS motion only. Optional --set ipc_socket=/private/path/control.sock enables read-only IPC.\n"
        "Duration0 runs until Ctrl-C/SIGTERM. Existing CAN link must already be configured and up.\n";
    if (argc == 2 && std::string_view{argv[1]} == "--help") {
        std::cout << usage;
        return 0;
    }
    if (argc < 3 || argc > 24 || std::string_view{argv[1]} != "--config") {
        std::cerr << usage;
        return 2;
    }
    bool check_only = false;
    try {
        std::vector<std::string> owned;
        for (int i = 3; i < argc; ++i) {
            const std::string_view option{argv[i]};
            if (option == "--check-config" && !check_only) {
                check_only = true;
                continue;
            }
            if (i + 1 >= argc)
                throw std::invalid_argument("missing option value");
            const std::string value{argv[++i]};
            if (option == "--set")
                owned.push_back(value);
            else if (option == "--interface")
                owned.push_back("interface=" + value);
            else if (option == "--device")
                owned.push_back("device=" + value);
            else if (option == "--log-dir")
                owned.push_back("log_dir=" + value);
            else if (option == "--duration-ms")
                owned.push_back("run_duration_ms=" + value);
            else
                throw std::invalid_argument("unknown option: " + std::string{option});
        }
        std::vector<std::string_view> overrides;
        overrides.reserve(owned.size());
        for (const auto& item : owned)
            overrides.push_back(item);
        std::ifstream input{argv[2]};
        if (!input)
            throw std::runtime_error("cannot open configuration file");
        config = rc::application::config::parse(input, overrides);
    } catch (const std::exception& error) {
        std::cerr << "configuration_error: " << error.what() << '\n';
        return 2;
    }
    if (check_only) {
        rc::application::config::dump(std::cout, config);
        return std::cout.good() ? 0 : 1;
    }
    return std::nullopt;
}
} // namespace

/** Compose the Debug-only SBUS test app with optional read-only IPC; validate before device activation. */
int main(int argc, char* argv[]) {
    rc::application::config::Startup config;
    if (const auto result = parse_command_line(argc, argv, config))
        return *result;
    try {
        if (::signal(SIGPIPE, SIG_IGN) == SIG_ERR)
            throw std::runtime_error("SIGPIPE setup failed");
        auto termination = rc::platform::linux::process::TerminationEvent::create();
        if (!termination.ok()) {
            report("termination", termination.status());
            return 1;
        }
        struct sigaction action{};
        action.sa_handler = hangup;
        if (::sigemptyset(&action.sa_mask) != 0 || ::sigaction(SIGHUP, &action, nullptr) != 0)
            throw std::runtime_error("SIGHUP setup failed");
        rc::app::LogSession logs{config};
        int result = 1;
        try {
            const auto pending = termination.value().consume();
            if (!pending.ok())
                report("pending_signal", pending.status());
            else
                result = pending.value() ? 128 + pending.value() : run(config, logs, termination.value());
        } catch (const std::exception& error) {
            std::cerr << "startup_error: " << error.what() << '\n';
        }
        const bool complete = logs.finish(result);
        std::cout << "result=" << result << " logs_complete=" << complete << " log_dir=" << logs.directory() << '\n';
        return complete ? result : 1;
    } catch (const std::exception& error) {
        std::cerr << "app_error: " << error.what() << '\n';
        return 1;
    }
}
