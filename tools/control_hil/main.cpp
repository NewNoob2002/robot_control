#include "application/control/linux_loop.hpp"
#include "communication/canopen/control_qualification.hpp"
#include "tools/control_hil/motion_gate.hpp"
#include "tools/control_hil/trace.hpp"

#include <algorithm>
#include <charconv>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <sstream>
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

/** Export only after stop/cleanup; bounded polling handles a temporarily full nonblocking pipe. */
bool export_trace(const hil::Trace& trace) {
    const auto deadline = Clock::now() + 5s;
    try {
        std::ostringstream formatted;
        if (!trace.dump(formatted))
            return false;
        const auto text = formatted.str();
        if (text.size() > hil::Trace::maximum_records * 512U + 4096U)
            return false;
        std::size_t offset = 0;
        while (offset < text.size() && Clock::now() < deadline) {
            const auto written =
                ::write(STDOUT_FILENO, text.data() + offset, std::min<std::size_t>(65536, text.size() - offset));
            if (written > 0) {
                offset += static_cast<std::size_t>(written);
                continue;
            }
            if (written < 0 && errno == EINTR)
                continue;
            if (written >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
                return false;
            pollfd output{STDOUT_FILENO, POLLOUT, 0};
            const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now());
            if (remaining <= 0ms)
                return false;
            const int ready = ::poll(&output, 1, static_cast<int>(remaining.count()));
            if (ready < 0 && errno == EINTR)
                continue;
            if (ready <= 0 || (output.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
                return false;
        }
        return offset == text.size() && std::cout.good();
    } catch (const std::bad_alloc&) {
        return false;
    }
}

/** Return the same accepted calibration and5rpm trial profile for R0 and motion. */
input::sbus::SourceConfig source_profile() {
    input::sbus::SourceConfig profile;
    profile.steering = {200, 1000, 1800, false};
    profile.throttle = {200, 993, 1800, false};
    profile.gear_rpm = {5, 5, 5};
    profile.maximum_rpm = 5;
    return profile;
}

/** Preserve raw-source linkage, normalized candidates and policy request without allocating. */
void record_cycle(hil::Trace& trace, const application::control::LoopResult& result) {
    const auto& source = result.source;
    const auto& selected = result.control.selected.command;
    const auto& request = result.control.request.decision;
    trace.append(
        hil::Trace::Kind::cycle, Clock::now(),
        {static_cast<std::int64_t>(result.cycles), static_cast<std::int64_t>(source.sample.sequence),
         static_cast<std::int64_t>(source.sample.authorization_generation),
         static_cast<std::int64_t>(source.sample.session_generation), hil::Trace::ns(source.sample.captured_at),
         source.steering, source.throttle, source.candidate.left_rpm, source.candidate.right_rpm, selected.left_rpm,
         selected.right_rpm, request.approved_command.left_rpm, request.approved_command.right_rpm,
         static_cast<std::int64_t>(request.action), source.sample.enabled, static_cast<std::int64_t>(source.health)});
}

/** Zero-only test observer; never changes a command, source sample or authorization. */
struct RecoveryTrial {
    int zero_tolerance{0};
    bool uart{false};
    bool partial_seen{false};
    bool uart_reacquired{false};
    bool resync_reported{false};
    Clock::time_point reconnect_deadline{};
    Clock::time_point uart_stable_since{};
    unsigned phase{0};
    std::uint64_t authorization{0}, system_authorization{0}, recovery_authorization{0};
    Clock::time_point since{};
    bool fault_seen{false}, button_seen{false}, quick_stop_seen{false}, disabled_seen{false};

    /**
     * Observe one real owner cycle and emit bounded operator phase prompts.
     * @param x1 True selects physical X1, false selects SBUS flags/staleness.
     * @param result Borrowed current cycle, never retained or modified.
     * @param state Borrowed runtime feedback, never retained or modified.
     * @param enabled_samples Number of initial zero-enabled observations.
     * @param batch Exact borrowed Reader batch; only F5 may accept one empty partial-timeout boundary.
     * @return Failure on unsafe evidence; phase6 denotes the completed scenario.
     * Single owner only; all timestamps use the local steady clock.
     */
    Status observe(bool x1, const application::control::LoopResult& result, const domain::drive::RuntimeState& state,
                   unsigned enabled_samples, const input::sbus::ReadBatch& batch) {
        const auto now = Clock::now();
        const auto fail = [&] {
            return Status::from_errno(
                "control_hil_recovery",
                "phase=" + std::to_string(phase)
                    + " source_fault=" + std::to_string(static_cast<unsigned>(result.source.fault))
                    + " status=" + std::to_string(state.feedback.status_raw) + " armed=" + std::to_string(state.armed)
                    + " word=" + std::to_string(std::to_integer<unsigned>(result.control.output.payload[0])),
                EPROTO);
        };
        const auto announce = [&](const char* name) {
            std::cout << "event=recovery phase=" << name << " kind=" << (x1 ? "x1" : "sbus")
                      << " at_ns=" << hil::Trace::ns(now)
                      << " source_authorization=" << result.source.sample.authorization_generation
                      << " system_authorization=" << result.control.request.authorization << '\n'
                      << std::flush;
        };
        const auto fresh = [now](Clock::time_point stamp, Clock::duration timeout) {
            return stamp >= Clock::time_point{} && stamp <= now && now - stamp < timeout;
        };
        const auto axes = domain::drive::decode_dual_axis_status(state.feedback.status_raw);
        const auto acceptable = [](domain::drive::Cia402State value) {
            return value != domain::drive::Cia402State::unknown && value != domain::drive::Cia402State::fault
                   && value != domain::drive::Cia402State::fault_reaction_active;
        };
        const auto& source = result.source;
        const bool active_x1 = (state.feedback.status_raw & 0x8000U) != 0;
        if (uart && batch.discontinuity != input::sbus::Discontinuity::none) {
            if (phase != 1 || fault_seen || partial_seen
                || batch.discontinuity != input::sbus::Discontinuity::partial_timeout
                || batch.raw_size != 0 || batch.event_count != 0
                || source.fault != input::sbus::InputFault::discontinuity)
                return fail();
            partial_seen = true;
            std::cout << "event=uart_boundary kind=partial_timeout at_ns=" << hil::Trace::ns(batch.captured_at)
                      << " session=" << batch.session << '\n' << std::flush;
        }
        if (uart && phase == 1 && fault_seen && batch.raw_size != 0)
            return fail(); // Bytes during the explicit silence hold invalidate F5.
        const bool source_fault = uart
                                  ? (source.fault == input::sbus::InputFault::timeout
                                     || (partial_seen && phase <= 2
                                         && source.fault == input::sbus::InputFault::discontinuity))
                                  : source.fault == input::sbus::InputFault::frame_lost
                                  || source.fault == input::sbus::InputFault::failsafe
                                  || source.fault == input::sbus::InputFault::timeout;
        const bool reacquiring = uart && phase == 2
                                 && source.fault == input::sbus::InputFault::rejected;
        if (uart && phase == 2 && now >= reconnect_deadline)
            return fail();
        if (uart && phase == 2) {
            const bool healthy_input = source.fault == input::sbus::InputFault::none
                                       && source.health == input::sbus::Health::disabled
                                       && fresh(source.sample.captured_at, 100ms);
            if (!healthy_input) {
                if ((reacquiring || uart_reacquired) && !resync_reported) {
                    resync_reported = true;
                    std::cout << "event=uart_resync at_ns=" << hil::Trace::ns(now)
                              << " authority=revoked" << '\n' << std::flush;
                }
                uart_stable_since = {};
                uart_reacquired = false;
                since = {};
                button_seen = false;
            } else if (!uart_reacquired) {
                if (source.steering != 0 || source.throttle != 0 || source.raw.channels[5] > 500) {
                    uart_stable_since = {};
                } else {
                    if (uart_stable_since == Clock::time_point{})
                        uart_stable_since = source.sample.captured_at;
                    if (source.sample.captured_at - uart_stable_since >= 1s) {
                        uart_reacquired = true;
                        resync_reported = false;
                        std::cout << "event=uart_reconnected at_ns=" << hil::Trace::ns(now)
                                  << " stable_since_ns=" << hil::Trace::ns(uart_stable_since)
                                  << " authority=revoked" << '\n' << std::flush;
                    }
                }
            }
        }
        const bool stimulus = x1 ? active_x1 : source_fault;
        const bool enabled = state.armed && axes.low_half.state == domain::drive::Cia402State::operation_enabled
                             && axes.high_half.state == domain::drive::Cia402State::operation_enabled;
        if (!state.feedback.current || !state.feedback.operational || state.feedback.mode_raw != 3
            || state.feedback.fault_raw != 0 || !fresh(state.feedback.heartbeat_at, 1000ms)
            || !fresh(state.feedback.status_at, 200ms) || !fresh(state.feedback.diagnostics_at, 200ms)
            || !acceptable(axes.low_half.state) || !acceptable(axes.high_half.state)
            || state.left_tenths_rpm < -zero_tolerance || state.left_tenths_rpm > zero_tolerance
            || state.right_tenths_rpm < -zero_tolerance || state.right_tenths_rpm > zero_tolerance
            || (state.feedback.status_raw & 0x80000000U) != 0 || (!x1 && active_x1)
            || (phase != 0 && source.fault != input::sbus::InputFault::none && (x1 || (!source_fault && !reacquiring))))
            return fail();
        if (phase == 0 && enabled && enabled_samples >= 20) {
            authorization = source.sample.authorization_generation;
            system_authorization = result.control.request.authorization;
            phase = 1;
            announce("fault_ready");
        }
        if (phase == 1 && stimulus && !fault_seen) {
            fault_seen = true;
            since = now;
            announce("fault_observed");
        }
        const auto word = result.control.output.payload[0];
        // The existing per-event inhibitor may publish zero Shutdown (6).
        if (fault_seen && phase < 4
            && (state.armed || (word != std::byte{0} && word != std::byte{2} && word != std::byte{6})))
            return fail();
        if (fault_seen && axes.low_half.state == domain::drive::Cia402State::quick_stop_active
            && axes.high_half.state == domain::drive::Cia402State::quick_stop_active) {
            quick_stop_seen = true;
            disabled_seen = false;
        }
        if (fault_seen && axes.low_half.state == domain::drive::Cia402State::switch_on_disabled
            && axes.high_half.state == domain::drive::Cia402State::switch_on_disabled)
            disabled_seen = true;
        if (phase == 1 && fault_seen) {
            if (!stimulus)
                return fail(); // The explicit physical hold must last at least one second.
            if (now - since >= (uart ? 5s : 1s)) {
                reconnect_deadline = now + 45s;
                phase = 2;
                since = {};
                announce("release_fault");
            }
        } else if (phase == 2) {
            const bool nonneutral = !source.candidate.is_zero() && source.fault == input::sbus::InputFault::none
                                    && fresh(source.sample.captured_at, 100ms);
            if (!stimulus && state.healthy && nonneutral && (!uart || uart_reacquired)) {
                if (since == Clock::time_point{})
                    since = now;
                button_seen = button_seen || source.raw.channels[5] >= 1500;
                if (now - since >= 1s && button_seen && source.raw.channels[5] <= 500 && !source.sample.enabled) {
                    phase = 3;
                    since = {};
                    announce("neutral_ready");
                }
            } else {
                since = {};
                button_seen = false;
            }
        } else if (phase == 3) {
            if (stimulus)
                return fail();
            if (state.healthy && source.fault == input::sbus::InputFault::none && source.steering == 0
                && source.throttle == 0 && source.raw.channels[5] <= 500 && !source.sample.enabled
                && fresh(source.sample.captured_at, 100ms)) {
                if (since == Clock::time_point{})
                    since = now;
                if (now - since >= 1s) {
                    phase = 4;
                    announce("rearm_ready");
                }
            } else {
                since = {};
            }
        } else if (phase >= 4) {
            if (source.sample.authorization_generation > authorization && recovery_authorization == 0)
                recovery_authorization = source.sample.authorization_generation;
            if (recovery_authorization != 0
                && (source.sample.authorization_generation != recovery_authorization || !source.sample.enabled))
                return fail(); // A later CH6 cycle cannot complete the first recovery attempt.
            if (stimulus || !state.healthy || source.steering != 0 || source.throttle != 0
                || (state.armed
                    && (source.sample.authorization_generation <= authorization
                        || result.control.request.authorization <= system_authorization)))
                return fail();
            if (enabled && (x1 || quick_stop_seen) && disabled_seen) {
                if (phase == 4) {
                    phase = 5;
                    since = now;
                    announce("zero_reenabled");
                } else if (now - since >= 1s) {
                    phase = 6;
                    announce("complete");
                }
            } else if (phase == 5) {
                return fail();
            }
        }
        return std::cout.good() ? Status::success() : fail();
    }
};

/** Receive-only R0: reuse the existing Reader/Source, never construct a CAN lifecycle. */
int observe_input(input::sbus::Reader& reader, platform::linux::process::TerminationEvent& termination,
                  hil::Trace& trace, std::chrono::milliseconds duration) {
    input::sbus::Source source{source_profile()};
    application::control::LoopResult result;
    auto status = Status::success();
    const auto until = Clock::now() + duration;
    std::cout << "event=input_observation_ready duration_ms=" << duration.count() << " drive=OFF ch6=released can=none"
              << '\n'
              << std::flush;
    while (Clock::now() < until && status.ok()) {
        const auto batch = reader.read(10ms, termination.fd());
        status = batch.status();
        if (batch.ok())
            trace.batch(batch.value());
        result.source = input::sbus::update_source(source, batch, Clock::now());
        ++result.cycles;
        record_cycle(trace, result);
        if (status.ok() && (trace.failed() || result.source.sample.enabled))
            status = Status::from_errno("control_hil_input_observation", "trace overflow or CH6 authorization", EACCES);
    }
    static_cast<void>(source.stop(Clock::now()));
    report("input_observation", status);
    const bool captured = export_trace(trace);
    if (!captured)
        report("trace_export", Status::from_errno("trace_export", "incomplete bounded output", EIO));
    return status.ok() && captured ? 0 : 1;
}

/** Run one bounded zero or selected-wheel session through the actual SBUS/control path. */
int run(const std::string& interface, std::chrono::milliseconds duration, const std::string& device,
        std::string_view mode, int zero_tolerance, std::chrono::milliseconds motion_window) {
    const bool input_only = mode == "--observe-input";
    const bool uart = mode == "--zero-uart-recovery";
    const bool recovery = mode == "--zero-x1-recovery" || mode == "--zero-sbus-recovery" || uart;
    RecoveryTrial recovery_trial{.zero_tolerance = zero_tolerance, .uart = uart};
    const bool throttle_only = mode == "--right-throttle";
    const bool motion = mode == "--single-left" || mode == "--single-right" || throttle_only;
    hil::Trace trace{input_only                                    ? hil::Trace::Mode::input_only
                     : !motion                                     ? hil::Trace::Mode::zero
                     : (mode == "--single-right" || throttle_only) ? hil::Trace::Mode::right
                                                                   : hil::Trace::Mode::left,
                     hil::Trace::maximum_records, zero_tolerance};
    if (trace.failed()) {
        report("trace_allocate", Status::from_errno("trace_allocate", "startup", ENOMEM));
        return 1;
    }
    const bool right = mode == "--single-right" || throttle_only;
    hil::MotionGate gate{right, motion_window};
    // The wrapper borrows this stack object only for the lifetime of this run.
    struct Binding {
        /** Bind on the sole owner thread; a null gate retains zero-only behavior. */
        explicit Binding(hil::MotionGate* value, hil::Trace& recorder) {
            hil::bind_motion_gate(value);
            hil::bind_trace(&recorder);
        }
        /** Remove the borrowed syscall context before its stack owner dies. */
        ~Binding() {
            hil::bind_motion_gate(nullptr);
            hil::bind_trace(nullptr);
        }
    } binding{motion ? &gate : nullptr, trace};
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
    if (input_only)
        return observe_input(reader, termination.value(), trace, duration);
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
    if (mode == "--restore-zero-baseline") {
        trace.watch_feedback();
        const auto restored = qualification.restore_interrupted(zero_tolerance);
        report("baseline_restore", restored);
        const bool captured = export_trace(trace);
        return restored.ok() && !trace.failed() && captured ? 0 : 1;
    }
    const auto proof = qualification.prepare(recovery, zero_tolerance);
    status = proof.status();
    application::control::LoopResult result;
    unsigned enabled_samples = 0;
    unsigned moving_feedback = 0;
    auto last_moving_feedback = Clock::time_point{};
    bool motion_ready = false;
    if (proof.ok()) {
        // The operator-calibrated fixed trial profile is injected at startup.
        const auto profile = source_profile();
        input::sbus::Source source{profile};
        trace.watch_feedback();
        application::control::CycleConfig config;
        config.runtime = {domain::drive::PackedHalf::low,
                          domain::drive::PackedHalf::low,
                          1,
                          1,
                          5,
                          zero_tolerance,
                          1000ms,
                          200ms,
                          50ms,
                          0x8000};
        config.arbiter.max_abs_rpm = 5;
        config.transition_timeout = 1000ms;
        auto attached = application::control::ControlLoop::create(*owner, reader, source, config, proof.value(), 100ms,
                                                                  throttle_only);
        status = attached.status();
        if (attached.ok()) {
            auto loop = std::move(attached).value();
            status = reader.open(device, serial); // Flush preparation backlog and require a fresh receiver session.
            const auto started = Clock::now();
            auto next_log = started;
            std::string_view previous_input_state;
            std::cout << "event=control_start duration_ms=" << duration.count()
                      << " targets=" << (motion ? (right ? "right_positive" : "left_positive") : "zero")
                      << " zero_feedback_tenths_rpm=" << zero_tolerance
                      << " motion_window_ms=" << motion_window.count()
                      << " input_profile=" << (throttle_only ? "right_throttle" : "differential")
                      << " operator=neutral_release_then_press" << '\n'
                      << std::flush;
            unsigned stop_cause =
                0; //0 duration,1 first-zero,2 motion-limit,3 gate rejection,4 runtime/signal,5 envelope,6 trace.
            input::sbus::ReadBatch captured_batch;
            while (status.ok() && Clock::now() - started < duration) {
                if (motion && gate.done(Clock::now())) {
                    const auto reason = gate.reason(Clock::now());
                    stop_cause = reason == hil::MotionGate::End::zero_target  ? 1U
                                 : reason == hil::MotionGate::End::time_limit ? 2U
                                                                              : 3U;
                    break;
                }
                const auto before = loop->runtime_state();
                result = loop->step({.coherent = true,
                                     .emergency_stop_known = before.healthy,
                                     .emergency_stop_active = (before.feedback.status_raw & 0x8000U) != 0},
                                    &captured_batch);
                if (captured_batch.session)
                    trace.batch(captured_batch);
                record_cycle(trace, result);
                const auto state = loop->runtime_state();
                status = result.status;
                if (result.finished) {
                    stop_cause = 4;
                    if (status.ok())
                        status = Status::from_errno("control_hil_interrupted", interface, ECANCELED);
                    break;
                }
                const auto selected_speed = right ? state.right_tenths_rpm : state.left_tenths_rpm;
                const auto other_speed = right ? state.left_tenths_rpm : state.right_tenths_rpm;
                const auto selected_candidate =
                    right ? result.source.candidate.right_rpm : result.source.candidate.left_rpm;
                const auto other_candidate =
                    right ? result.source.candidate.left_rpm : result.source.candidate.right_rpm;
                const bool awaiting_input = throttle_only && enabled_samples == 0 && !result.source.sample.enabled;
                const bool envelope =
                    motion
                        ? other_speed >= -zero_tolerance && other_speed <= zero_tolerance
                              && (gate.started()
                                      ? (selected_speed >= -zero_tolerance && selected_speed <= 75)
                                      : (selected_speed >= -zero_tolerance && selected_speed <= zero_tolerance))
                              && (awaiting_input || ((throttle_only ? result.source.steering == 0 : other_candidate == 0)
                                                     && selected_candidate >= 0 && selected_candidate <= 5))
                        : state.left_tenths_rpm >= -zero_tolerance && state.left_tenths_rpm <= zero_tolerance
                              && state.right_tenths_rpm >= -zero_tolerance && state.right_tenths_rpm <= zero_tolerance
                              && result.source.candidate.left_rpm == 0 && result.source.candidate.right_rpm == 0;
                if (!recovery && (!state.healthy || (enabled_samples > 0 && !state.armed) || !envelope))
                    status = Status::from_errno(
                        motion ? "control_hil_motion_envelope" : "control_hil_zero_envelope",
                        interface + " healthy=" + std::to_string(state.healthy)
                            + " armed=" + std::to_string(state.armed)
                            + " reason=" + std::to_string(static_cast<unsigned>(state.output.reason))
                            + " status=" + std::to_string(state.feedback.status_raw) + " source_fault="
                            + std::to_string(static_cast<unsigned>(result.source.fault)) + " source_age_ms="
                            + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 Clock::now() - result.source.sample.captured_at)
                                                 .count()),
                        EACCES);
                if (!status.ok())
                    stop_cause = 5;
                if (status.ok() && trace.failed()) {
                    stop_cause = 6;
                    status = Status::from_errno("control_hil_trace_failure", interface, EPROTO);
                }
                const auto axes = domain::drive::decode_dual_axis_status(state.feedback.status_raw);
                if (state.armed && axes.low_half.state == domain::drive::Cia402State::operation_enabled
                    && axes.high_half.state == domain::drive::Cia402State::operation_enabled)
                    ++enabled_samples;
                if (throttle_only && status.ok() && !motion_ready && enabled_samples == 0) {
                    const auto& source_state = result.source;
                    const std::string_view input_state =
                        source_state.sample.enabled ? "arming"
                        : source_state.health != input::sbus::Health::disabled ? "waiting_link"
                        : source_state.steering != 0 || source_state.throttle != 0 ? "neutral_required"
                        : source_state.raw.channels[profile.channels[2]] > profile.button_release ? "release_ch6"
                                                                                       : "ready";
                    if (input_state != previous_input_state) {
                        std::cout << "event=input_status state=" << input_state
                                  << " source_fault=" << static_cast<unsigned>(source_state.fault)
                                  << " flags=" << static_cast<unsigned>(source_state.raw.raw_flags) << '\n'
                                  << std::flush;
                        previous_input_state = input_state;
                    }
                }
                if (recovery && status.ok()) {
                    status = recovery_trial.observe(mode == "--zero-x1-recovery", result, state, enabled_samples, captured_batch);
                    if (!status.ok() || recovery_trial.phase == 6) {
                        stop_cause = status.ok() ? 7U : 5U;
                        break;
                    }
                }
                if (motion && status.ok() && !motion_ready && enabled_samples >= 20 && selected_speed >= -zero_tolerance
                    && selected_speed <= zero_tolerance && selected_candidate == 0) {
                    gate.arm();
                    motion_ready = true;
                    std::cout << "event=motion_ready wheel=" << (right ? "right" : "left")
                              << " max_rpm=5 maximum_nonzero_ms=" << motion_window.count() << '\n'
                              << std::flush;
                }
                if (motion && gate.started() && selected_speed > zero_tolerance
                    && state.feedback.status_at > last_moving_feedback) {
                    last_moving_feedback = state.feedback.status_at;
                    ++moving_feedback;
                }
                if (Clock::now() >= next_log) {
                    next_log = Clock::now() + 100ms;
                    std::cout << "event=control elapsed_ms="
                              << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count()
                              << " source_health=" << static_cast<unsigned>(result.source.health)
                              << " authorization=" << result.source.sample.authorization_generation
                              << " armed=" << state.armed << " status=" << state.feedback.status_raw
                              << " word=" << std::to_integer<unsigned>(state.output.payload[0])
                              << " left_tenths_rpm=" << state.left_tenths_rpm
                              << " right_tenths_rpm=" << state.right_tenths_rpm
                              << " enabled_samples=" << enabled_samples << '\n'
                              << std::flush;
                    if (!std::cout.good())
                        status = Status::from_errno("control_hil_output", interface, EIO);
                }
            }
            gate.close();
            if (recovery && status.ok() && recovery_trial.phase != 6)
                status = Status::from_errno("control_hil_recovery_incomplete", interface, ENODATA);
            const auto stop_started = Clock::now();
            trace.append(hil::Trace::Kind::stop, stop_started,
                         {stop_cause, static_cast<std::int64_t>(result.exit),
                          static_cast<std::int64_t>(result.source.sample.sequence),
                          hil::Trace::ns(result.source.sample.captured_at)});
            result = loop->stop();
            std::cout << "event=stop_cause code=" << stop_cause << " signal_exit=" << static_cast<unsigned>(result.exit)
                      << " at_ns=" << hil::Trace::ns(stop_started) << '\n'
                      << std::flush;
            if (status.ok())
                status = result.status;
            if (status.ok() && enabled_samples < 10)
                status = Status::from_errno("control_hil_no_zero_enable", interface, ENODATA);
            report("runtime_stop", result.stop_status);
            if (motion && gate.started() && result.stop_status.ok()) {
                // Observe a new zero-speed TPDO for 150ms before SDO restoration.
                // The one-second physical-stop bound is separate from syscall completion.
                auto zero_since = Clock::time_point{};
                auto verified_zero_at = Clock::time_point{};
                bool capture_uart = true;
                bool stopped = false;
                while (Clock::now() - stop_started < 1s) {
                    const auto event = owner->run_until(Clock::now() + 1ms);
                    if (!event.ok() || event.value() != communication::canopen::LifecycleExit::deadline)
                        break;
                    if (capture_uart) {
                        const auto batch = reader.read(0ms);
                        if (batch.ok())
                            trace.batch(batch.value());
                        else {
                            capture_uart = false;
                            if (status.ok())
                                status = batch.status();
                        }
                    }
                    // RuntimePolicy is terminal after stop; observe the live owner, not its frozen state.
                    if (status.ok() && trace.failed())
                        status =
                            Status::from_errno("control_hil_trace_failure", "stopping feedback or evidence", EPROTO);
                    const auto snapshot = owner->observation_snapshot(Clock::now());
                    const auto& feedback = snapshot.tpdo[0];
                    if (snapshot.generation != proof.value().generation || !feedback.current
                        || feedback.raw.received_at <= stop_started) {
                        zero_since = {};
                        verified_zero_at = {};
                        continue;
                    }
                    /** Check one signed raw axis against the explicit standstill tolerance. */
                    const auto in_standstill_range = [&](unsigned offset) {
                        const auto lo = static_cast<unsigned>(feedback.raw.payload[offset]);
                        const auto hi = static_cast<unsigned>(feedback.raw.payload[offset + 1]);
                        const int raw = static_cast<int>(lo | (hi << 8U));
                        const int speed = raw >= 32768 ? raw - 65536 : raw;
                        return speed >= -zero_tolerance && speed <= zero_tolerance;
                    };
                    if (in_standstill_range(4) && in_standstill_range(6)) {
                        if (zero_since == Clock::time_point{})
                            zero_since = feedback.raw.received_at;
                        // Keep one additional fresh zero frame after the150ms criterion;
                        // independent capture timestamps must prove the same interval without rounding slack.
                        if (verified_zero_at != Clock::time_point{} && feedback.raw.received_at > verified_zero_at) {
                            stopped = true;
                            break;
                        }
                        if (feedback.raw.received_at - zero_since >= 150ms && verified_zero_at == Clock::time_point{})
                            verified_zero_at = feedback.raw.received_at;
                    } else {
                        zero_since = {};
                        verified_zero_at = {};
                    }
                }
                std::cout << "event=motion_stop verified=" << stopped << " elapsed_us="
                          << std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - stop_started).count()
                          << " moving_feedback=" << moving_feedback << '\n'
                          << std::flush;
                if (status.ok() && !stopped)
                    status = Status::from_errno("control_hil_motion_stop_deadline", interface, ETIMEDOUT);
            }
            if (status.ok() && motion && (!gate.started() || moving_feedback < 2))
                status = Status::from_errno("control_hil_no_motion_feedback", interface, ENODATA);
            // Detach the sole RPDO writer before any SDO cleanup.
            loop.reset();
        }
    }
    const auto cleanup = qualification.finish();
    if (status.ok() && trace.failed())
        status = Status::from_errno("control_hil_trace_failure", "feedback or evidence incomplete", EPROTO);
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
    const bool captured = export_trace(trace);
    if (!captured)
        report("trace_export", Status::from_errno("trace_export", "incomplete bounded output", EIO));
    return status.ok() && cleanup.ok() && captured ? 0 : 1;
}
} // namespace

/** Explicit bounded qualification CLI; single-wheel selection never authorizes an automatic retry. */
int main(int argc, char* argv[]) {
    const std::string_view usage =
        "Usage: robot-control-hil --interface can0|none --device /dev/tty... --duration-ms "
        "MS "
        "(--zero-only|--single-left|--single-right|--right-throttle|--observe-input|--zero-x1-recovery|--zero-sbus-"
        "recovery|--zero-uart-recovery|--restore-"
        "zero-baseline)\n"
        "Optional for control/restore: --zero-feedback-tenths-rpm 0..20 (default20); standstill only; "
        "moving direction/target bounds unchanged.\n"
        "Optional for motion: --motion-window-ms 3000..8000 (default3000); cutoff50ms before limit.\n"
        "MS: 2000..20000 for zero-only; 2000..120000 for zero-uart-recovery; "
        "2000..60000 for other bounded trials.\n";
    if (argc == 2 && std::string_view{argv[1]} == "--help") {
        std::cout << usage;
        return 0;
    }
    if ((argc != 8 && argc != 10 && argc != 12) || std::string_view{argv[1]} != "--interface"
        || std::string_view{argv[3]} != "--device" || std::string_view{argv[5]} != "--duration-ms"
        || (std::string_view{argv[7]} != "--zero-only" && std::string_view{argv[7]} != "--single-left"
            && std::string_view{argv[7]} != "--single-right" && std::string_view{argv[7]} != "--right-throttle"
            && std::string_view{argv[7]} != "--observe-input" && std::string_view{argv[7]} != "--zero-x1-recovery"
            && std::string_view{argv[7]} != "--zero-sbus-recovery"
            && std::string_view{argv[7]} != "--zero-uart-recovery"
            && std::string_view{argv[7]} != "--restore-zero-baseline")) {
        std::cerr << usage;
        return 2;
    }
    int zero_tolerance = std::string_view{argv[7]} == "--observe-input" ? 0 : 20;
    auto motion_window = hil::MotionGate::default_window;
    bool tolerance_set = false, window_set = false;
    for (int index = 8; index < argc; index += 2) {
        const std::string_view name{argv[index]}, value{argv[index + 1]}, mode{argv[7]};
        int number = 0;
        const auto parsed_option = std::from_chars(value.data(), value.data() + value.size(), number);
        const bool tolerance = name == "--zero-feedback-tenths-rpm" && !tolerance_set
                               && mode != "--observe-input" && number >= 0 && number <= 20;
        const bool window = name == "--motion-window-ms" && !window_set
                            && (mode == "--single-left" || mode == "--single-right" || mode == "--right-throttle")
                            && number >= hil::MotionGate::default_window.count()
                            && number <= hil::MotionGate::maximum_window.count();
        if (parsed_option.ec != std::errc{} || parsed_option.ptr != value.data() + value.size()
            || (!tolerance && !window)) {
            std::cerr << usage;
            return 2;
        }
        if (tolerance) {
            zero_tolerance = number;
            tolerance_set = true;
        } else {
            motion_window = std::chrono::milliseconds{number};
            window_set = true;
        }
    }
    const std::string interface{argv[2]}, device{argv[4]};
    const std::string_view duration{argv[6]};
    unsigned milliseconds = 0;
    const auto parsed = std::from_chars(duration.data(), duration.data() + duration.size(), milliseconds);
    const auto* fixture = std::getenv("ROBOT_CONTROL_TEST_VCAN_INTERFACE");
    if (parsed.ec != std::errc{} || parsed.ptr != duration.data() + duration.size() || milliseconds < 2000
        || milliseconds > (std::string_view{argv[7]} == "--zero-only" ? 20000U
                            : std::string_view{argv[7]} == "--zero-uart-recovery" ? 120000U : 60000U) || !device.starts_with("/dev/")
        || (interface != (std::string_view{argv[7]} == "--observe-input" ? "none" : "can0")
            && !(interface == "vcan0" && fixture && std::string_view{fixture} == "vcan0"))) {
        std::cerr << usage;
        return 2;
    }
    // Blocking diagnostic output must not hold an enabled drive indefinitely.
    const int flags = ::fcntl(STDOUT_FILENO, F_GETFL);
    const int err_flags = ::fcntl(STDERR_FILENO, F_GETFL);
    if (flags < 0 || err_flags < 0 || ::fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK) < 0
        || ::fcntl(STDERR_FILENO, F_SETFL, err_flags | O_NONBLOCK) < 0 || ::signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        return 1;
    const auto result =
        run(interface, std::chrono::milliseconds{milliseconds}, device, argv[7], zero_tolerance, motion_window);
    static_cast<void>(::fcntl(STDOUT_FILENO, F_SETFL, flags));
    static_cast<void>(::fcntl(STDERR_FILENO, F_SETFL, err_flags));
    return result;
}
