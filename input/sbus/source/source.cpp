#include "input/sbus/source/source.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace robot_control::input::sbus {
namespace {
/** Validate one strictly ordered 11-bit calibration. */
bool valid_axis(const AxisConfig& axis) noexcept {
    return axis.minimum < axis.center && axis.center < axis.maximum && axis.maximum <= 2047;
}
/** Normalize using asymmetric spans, truncation, inversion and inclusive deadband. */
std::int32_t normalize(std::uint16_t raw, const AxisConfig& axis, std::int32_t deadband) noexcept {
    const auto value = static_cast<std::int32_t>(raw);
    const auto delta = value - axis.center;
    auto normalized = delta * 1000 / (delta < 0 ? axis.center - axis.minimum : axis.maximum - axis.center);
    normalized = std::clamp(normalized, -1000, 1000);
    if (axis.reversed)
        normalized = -normalized;
    return std::abs(normalized) <= deadband ? 0 : normalized;
}
/** Apply the wide integer mixer and exclusive output deadband. */
std::int32_t mix(std::int32_t input, std::int32_t gear, const SourceConfig& config) noexcept {
    const auto product = static_cast<std::int64_t>(std::clamp(input, -1000, 1000)) * gear;
    const auto rpm = static_cast<std::int32_t>(std::clamp(
        product / 1000, -static_cast<std::int64_t>(config.maximum_rpm), static_cast<std::int64_t>(config.maximum_rpm)));
    return std::abs(rpm) < config.output_deadband_rpm ? 0 : rpm;
}
/** Refuse inconsistent hand-built frames as well as out-of-range channels. */
bool valid_frame(const protocol::Frame& frame) noexcept {
    return std::all_of(frame.channels.begin(), frame.channels.end(),
                       [](auto channel) {
                           return channel <= 2047;
                       })
           && frame.frame_lost == ((frame.raw_flags & 4U) != 0) && frame.failsafe == ((frame.raw_flags & 8U) != 0)
           && frame.digital_channel_17 == ((frame.raw_flags & 1U) != 0)
           && frame.digital_channel_18 == ((frame.raw_flags & 2U) != 0);
}
} // namespace

ConfigError validate(const SourceConfig& config) noexcept {
    for (std::size_t i = 0; i < config.channels.size(); ++i) {
        if (config.channels[i] >= 16)
            return ConfigError::channels;
        for (std::size_t j = 0; j < i; ++j)
            if (config.channels[i] == config.channels[j])
                return ConfigError::channels;
    }
    if (!valid_axis(config.steering) || !valid_axis(config.throttle))
        return ConfigError::calibration;
    if (config.deadband < 0 || config.deadband >= 1000)
        return ConfigError::deadband;
    if (config.button_release >= config.button_press || config.button_press > 2047
        || config.gear_low >= config.gear_high || config.gear_high > 2047)
        return ConfigError::thresholds;
    if (config.gear_rpm[0] <= 0 || config.gear_rpm[0] > config.gear_rpm[1] || config.gear_rpm[1] > config.gear_rpm[2]
        || config.gear_rpm[2] > config.maximum_rpm || config.output_deadband_rpm < 0
        || config.output_deadband_rpm > config.maximum_rpm)
        return ConfigError::limits;
    if (config.timeout <= Duration::zero() || config.button_cooldown < Duration::zero() || config.recovery_frames == 0)
        return ConfigError::timing;
    return ConfigError::none;
}

Source::Source(SourceConfig config) : config_(config) {
    state_.sample.source = domain::command::Source::sbus;
    state_.sample.coherent = true;
    state_.config_error = validate(config_);
    if (state_.config_error != ConfigError::none) {
        revoke(InputFault::configuration, {});
        terminal_ = true;
    }
}

void Source::revoke(InputFault fault, MonotonicTime now) {
    state_.sample.command = {};
    state_.sample.valid = false;
    state_.sample.enabled = false;
    state_.health = Health::faulted;
    state_.fault = state_.last_fault = fault;
    state_.last_fault_at = now;
    state_.last_fault_flags = 0;
    state_.recovery_count = 0;
    button_down_ = false;
    released_ = false;
}

bool Source::advance_time(MonotonicTime now) {
    if (now < MonotonicTime{} || (have_now_ && now < last_now_)) {
        revoke(InputFault::clock_regression, last_now_);
        require_new_session_ = true;
        return false;
    }
    have_now_ = true;
    last_now_ = now;
    if (state_.has_frame && state_.health != Health::faulted && now - state_.sample.captured_at >= config_.timeout)
        revoke(InputFault::timeout, now);
    return true;
}

void Source::publish() {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (state_.sample.sequence >= maximum - 1) {
        revoke(InputFault::counter_exhausted, last_now_);
        terminal_ = true;
        state_.sample.sequence = maximum;
        return;
    }
    ++state_.sample.sequence;
}

void Source::accept_frame(const protocol::Frame& frame, MonotonicTime received, bool eligible, bool count_recovery) {
    state_.raw = frame;
    state_.has_frame = true;
    state_.sample.captured_at = received;
    state_.sample.lost = frame.frame_lost;
    state_.sample.failsafe = frame.failsafe;
    state_.steering = normalize(frame.channels[config_.channels[0]], config_.steering, config_.deadband);
    state_.throttle = normalize(frame.channels[config_.channels[1]], config_.throttle, config_.deadband);
    const auto gear_raw = frame.channels[config_.channels[3]];
    const auto gear = config_.gear_rpm[gear_raw <= config_.gear_low ? 0U : (gear_raw >= config_.gear_high ? 2U : 1U)];
    state_.candidate = {mix(state_.throttle + state_.steering, gear, config_),
                        mix(state_.throttle - state_.steering, gear, config_), false};
    if (!eligible)
        return;
    if (count_recovery && state_.recovery_count < config_.recovery_frames)
        ++state_.recovery_count;
    if (state_.recovery_count < config_.recovery_frames) {
        state_.health = Health::recovering;
        state_.fault = InputFault::none;
        return;
    }
    state_.fault = InputFault::none;
    state_.health = state_.sample.enabled ? Health::enabled : Health::disabled;
    // Only the first healthy event of a distinct read may establish an edge.
    if (count_recovery) {
        const auto button = frame.channels[config_.channels[2]];
        if (button <= config_.button_release) {
            button_down_ = false;
            released_ = true;
        } else if (button >= config_.button_press) {
            const bool edge = released_ && !button_down_;
            button_down_ = true;
            released_ = false; // A premature/nonneutral edge is consumed, never queued.
            const bool cooled = !have_toggle_ || received - last_toggle_ >= config_.button_cooldown;
            const bool neutral = state_.steering == 0 && state_.throttle == 0;
            if (edge && cooled && (state_.sample.enabled || neutral)) {
                if (!state_.sample.enabled) {
                    if (state_.sample.authorization_generation == std::numeric_limits<std::uint64_t>::max()) {
                        revoke(InputFault::counter_exhausted, last_now_);
                        terminal_ = true;
                        return;
                    }
                    ++state_.sample.authorization_generation;
                }
                state_.sample.enabled = !state_.sample.enabled;
                last_toggle_ = received;
                have_toggle_ = true;
            }
        }
    }
    state_.sample.valid = state_.sample.enabled;
    state_.health = state_.sample.enabled ? Health::enabled : Health::disabled;
    state_.sample.command = state_.sample.enabled ? state_.candidate : domain::command::MotionCommand{};
}

SourceSnapshot Source::consume(Observation observation, MonotonicTime now) {
    const std::scoped_lock lock{mutex_};
    if (terminal_)
        return state_;
    if (!advance_time(now)) {
        publish();
        return state_;
    }
    if (observation.session == 0 || observation.session < state_.sample.session_generation) {
        revoke(InputFault::replay, now);
        publish();
        return state_;
    }
    if (observation.captured_at < MonotonicTime{} || observation.captured_at > now) {
        revoke(InputFault::future_time, now);
        publish();
        return state_;
    }
    if (now - observation.captured_at >= config_.timeout) {
        revoke(InputFault::timeout, now);
        publish();
        return state_;
    }
    const bool new_session = observation.session > state_.sample.session_generation;
    if ((have_batch_ && observation.captured_at <= last_batch_) || (require_new_session_ && !new_session)) {
        revoke(InputFault::replay, now);
        publish();
        return state_;
    }
    if (new_session) {
        if (state_.sample.session_generation != 0)
            revoke(InputFault::session_changed, now);
        state_.sample.session_generation = observation.session;
        require_new_session_ = false;
    }
    have_batch_ = true;
    last_batch_ = observation.captured_at;
    if (observation.discontinuous) {
        revoke(InputFault::discontinuity, now);
        publish();
        return state_;
    }
    if (observation.events.size() > 256) {
        revoke(InputFault::malformed, now);
        publish();
        return state_;
    }
    const auto authorization_before = state_.sample.authorization_generation;
    bool batch_fault = false;
    bool counted = false;
    for (const auto& event : observation.events) {
        if (event.kind != protocol::EventKind::frame || !valid_frame(event.frame)) {
            revoke(event.kind == protocol::EventKind::rejected ? InputFault::rejected : InputFault::malformed, now);
            batch_fault = true;
            continue;
        }
        if (event.frame.frame_lost || event.frame.failsafe) {
            revoke(event.frame.failsafe ? InputFault::failsafe : InputFault::frame_lost, now);
            state_.last_fault_flags = event.frame.raw_flags;
            batch_fault = true;
        }
        accept_frame(event.frame, observation.captured_at, !batch_fault, !counted);
        counted = true;
        if (terminal_)
            break;
    }
    if (state_.sample.enabled && state_.sample.authorization_generation != authorization_before)
        state_.sample.command = {0, 0, false}; // Every rearm read publishes zero, including later coalesced frames.
    publish();
    return state_;
}

SourceSnapshot Source::tick(MonotonicTime now) {
    const std::scoped_lock lock{mutex_};
    if (terminal_)
        return state_;
    const auto before = state_.health;
    const auto previous_fault = state_.fault;
    const bool valid_time = advance_time(now);
    if (!valid_time || state_.health != before || state_.fault != previous_fault)
        publish();
    return state_;
}

SourceSnapshot Source::transport_error(MonotonicTime now) {
    const std::scoped_lock lock{mutex_};
    if (terminal_)
        return state_;
    if (advance_time(now))
        revoke(InputFault::transport, now);
    require_new_session_ = true;
    publish();
    return state_;
}

SourceSnapshot Source::stop(MonotonicTime now) {
    const std::scoped_lock lock{mutex_};
    if (terminal_)
        return state_;
    if (advance_time(now))
        revoke(InputFault::shutdown, now);
    require_new_session_ = true;
    publish();
    return state_;
}

SourceSnapshot Source::snapshot() const {
    const std::scoped_lock lock{mutex_};
    return state_;
}
} // namespace robot_control::input::sbus
