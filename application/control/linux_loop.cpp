#include "application/control/linux_loop.hpp"

#include <algorithm>
#include <cerrno>
#include <limits>
#include <new>

namespace robot_control::application::control {
namespace {
using communication::canopen::LifecycleExit;
using platform::linux::Status;
using Clock = time::MonotonicClock;
using namespace std::chrono_literals;
} // namespace

ControlLoop::ControlLoop(communication::canopen::Lifecycle& owner, input::sbus::Reader& reader,
                         input::sbus::Source& source, CycleConfig config,
                         std::unique_ptr<communication::canopen::RuntimeSession> runtime,
                         time::Duration shutdown_timeout, bool right_throttle_only)
    : owner_{owner}, reader_{reader}, source_{source}, config_{config}, shutdown_timeout_{shutdown_timeout},
      right_throttle_only_{right_throttle_only}, runtime_{std::move(runtime)}, cycle_{config},
      next_cycle_{Clock::now()} {}

ControlLoop::CreateResult ControlLoop::create(communication::canopen::Lifecycle& owner, input::sbus::Reader& reader,
                                              input::sbus::Source& source, CycleConfig config,
                                              const communication::canopen::RuntimeLayoutProof& proof,
                                              time::Duration shutdown_timeout, bool right_throttle_only) {
    if (!valid_cycle_config(config) || config.period > 1s || shutdown_timeout <= time::Duration::zero()
        || shutdown_timeout > 1s || source.snapshot().config_error != input::sbus::ConfigError::none)
        return CreateResult::failure(Status::from_errno("control_loop_config", "startup", EINVAL));
    const auto serial = reader.configuration();
    if (!serial.ok())
        return CreateResult::failure(serial.status());
    auto runtime = communication::canopen::RuntimeSession::create(owner, config.runtime, proof);
    if (!runtime.ok())
        return CreateResult::failure(runtime.status());
    auto loop = std::unique_ptr<ControlLoop>{new (std::nothrow) ControlLoop(
        owner, reader, source, config, std::move(runtime).value(), shutdown_timeout, right_throttle_only)};
    if (!loop)
        return CreateResult::failure(Status::from_errno("control_loop_allocate", "startup", ENOMEM));
    // A previous loop's enabled sample cannot authorize its replacement.
    loop->result_.source = source.stop(Clock::now());
    loop->result_.runtime_session = loop->runtime_->session();
    return CreateResult::success(std::move(loop));
}

drive::RuntimeState ControlLoop::runtime_state() const noexcept {
    return runtime_->state();
}

LoopResult ControlLoop::finish(Status status, LifecycleExit exit, time::MonotonicTime started) {
    if (result_.finished)
        return result_;
    const auto start = Clock::now();
    result_.source = source_.stop(start);
    result_.control = cycle_.prepare({.shutdown_requested = true}, runtime_->state(), start);
    result_.stop_status = runtime_->stop();
    const auto end = Clock::now();
    cycle_.complete(result_.control, runtime_->state(), end);
    result_.shutdown_elapsed = end - started;
    result_.status = std::move(status);
    if (result_.status.ok() && !result_.stop_status.ok())
        result_.status = result_.stop_status;
    if (result_.status.ok() && result_.shutdown_elapsed >= shutdown_timeout_)
        result_.status = Status::from_errno("control_loop_shutdown_deadline",
                                            "session=" + std::to_string(runtime_->session()), ETIMEDOUT);
    result_.exit = exit;
    result_.finished = true;
    return result_;
}

LoopResult ControlLoop::stop() {
    return finish(Status::success(), LifecycleExit::quit, Clock::now());
}

LoopResult ControlLoop::step(CycleInput input, input::sbus::ReadBatch* captured_batch) {
    if (captured_batch)
        *captured_batch = {};
    if (result_.finished)
        return result_;
    if (input.shutdown_requested)
        return stop();
    const auto event_started = Clock::now();
    const auto event = owner_.run_until(next_cycle_);
    if (!event.ok())
        return finish(event.status(), LifecycleExit::quit, event_started);
    if (event.value() != LifecycleExit::deadline)
        return finish(Status::success(), event.value(), event_started);
    const auto start = Clock::now();
    result_.maximum_lateness = std::max(result_.maximum_lateness, start - next_cycle_);
    const auto batch = reader_.read(0ms);
    if (captured_batch && batch.ok())
        *captured_batch = batch.value();
    result_.source = input::sbus::update_source(source_, batch, Clock::now());
    if (!batch.ok())
        return finish(batch.status(), LifecycleExit::quit, start);
    input.sbus = result_.source.sample;
    if (right_throttle_only_) {
        if (input.sbus.valid && input.sbus.enabled && (result_.source.steering != 0 || result_.source.throttle < 0))
            return finish(Status::from_errno("control_loop_right_throttle", "steering or reverse input", EACCES),
                          LifecycleExit::quit, start);
        // Preserve Source and all authority metadata; project only this qualification command.
        input.sbus.command.left_rpm = 0;
    }
    input.coherent = input.coherent && input.sbus.coherent;
    const auto now = Clock::now();
    result_.control = cycle_.prepare(input, runtime_->state(), now);
    const auto sent = runtime_->submit({result_.control.request, runtime_->session()});
    cycle_.complete(result_.control, runtime_->state(), Clock::now());
    if (!sent.ok())
        return finish(sent.status(), LifecycleExit::quit, start);
    const auto end = Clock::now();
    result_.maximum_cycle_time = std::max(result_.maximum_cycle_time, end - start);
    if (result_.cycles == std::numeric_limits<std::uint64_t>::max())
        return finish(Status::from_errno("control_loop_counter", "cycles", EOVERFLOW), LifecycleExit::quit, start);
    ++result_.cycles;
    next_cycle_ += config_.period;
    if (end >= next_cycle_) {
        const auto missed = static_cast<std::uint64_t>((end - next_cycle_) / config_.period) + 1;
        result_.missed_periods += std::min(missed, std::numeric_limits<std::uint64_t>::max() - result_.missed_periods);
        next_cycle_ = end + config_.period;
    }
    return result_;
}
} // namespace robot_control::application::control
