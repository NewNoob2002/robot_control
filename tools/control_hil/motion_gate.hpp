#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

namespace robot_control::hil {
/** Explicit qualification profiles; values 0..3 retain the historical trace modes. */
enum class MotionMode : std::uint8_t {
    input_only,
    zero,
    left,
    right,
    left_negative,
    right_negative,
    dual_positive,
    dual_negative,
    turn_left,
    turn_right,
    differential_left,
    differential_right
};

/** Parse only motion CLI modes; unknown names return zero and must be validated by the caller. Pure, no ownership. */
[[nodiscard]] inline MotionMode motion_mode(std::string_view name) noexcept {
    if (name == "--single-left")
        return MotionMode::left;
    if (name == "--single-right" || name == "--right-throttle")
        return MotionMode::right;
    if (name == "--single-left-negative")
        return MotionMode::left_negative;
    if (name == "--single-right-negative")
        return MotionMode::right_negative;
    if (name == "--dual-positive")
        return MotionMode::dual_positive;
    if (name == "--dual-negative")
        return MotionMode::dual_negative;
    if (name == "--turn-left")
        return MotionMode::turn_left;
    if (name == "--turn-right")
        return MotionMode::turn_right;
    if (name == "--differential-left")
        return MotionMode::differential_left;
    if (name == "--differential-right")
        return MotionMode::differential_right;
    return MotionMode::zero;
}

/** Return the required sign of one wheel, or zero for an uncommanded wheel/invalid mode. Pure, reentrant. */
[[nodiscard]] inline int axis_direction(MotionMode mode, bool right) noexcept {
    switch (mode) {
        case MotionMode::left:
            return right ? 0 : 1;
        case MotionMode::right:
            return right ? 1 : 0;
        case MotionMode::left_negative:
            return right ? 0 : -1;
        case MotionMode::right_negative:
            return right ? -1 : 0;
        case MotionMode::dual_positive:
        case MotionMode::differential_left:
        case MotionMode::differential_right:
            return 1;
        case MotionMode::dual_negative:
            return -1;
        case MotionMode::turn_left:
            return right ? 1 : -1;
        case MotionMode::turn_right:
            return right ? -1 : 1;
        default:
            return 0;
    }
}

/** Return whether the profile requires differential evidence after its permitted straight approach. Pure, reentrant. */
[[nodiscard]] inline bool is_differential(MotionMode mode) noexcept {
    return mode == MotionMode::differential_left || mode == MotionMode::differential_right;
}

/** Check signed targets without clamping; differential profiles permit positive equal-wheel approach. Pure, reentrant. */
[[nodiscard]] inline bool targets_match(MotionMode mode, int left, int right) noexcept {
    if (left == 0 && right == 0)
        return true;
    if (left < -5 || left > 5 || right < -5 || right > 5)
        return false;
    const int left_sign = axis_direction(mode, false), right_sign = axis_direction(mode, true);
    if ((left_sign ? left * left_sign < 1 : left != 0) || (right_sign ? right * right_sign < 1 : right != 0))
        return false;
    switch (mode) {
        case MotionMode::left:
        case MotionMode::right:
        case MotionMode::left_negative:
        case MotionMode::right_negative:
            return true;
        case MotionMode::dual_positive:
        case MotionMode::dual_negative:
            return left == right;
        case MotionMode::turn_left:
        case MotionMode::turn_right:
            return left == -right;
        case MotionMode::differential_left:
            return left <= right;
        case MotionMode::differential_right:
            return left >= right;
        default:
            return false;
    }
}

/** Check signed measured feedback: startup/uncommanded wheels use tolerance, moving wheels allow up to 7.5rpm. */
[[nodiscard]] inline bool feedback_matches(MotionMode mode, bool right, int speed, int tolerance,
                                           bool started) noexcept {
    const int sign = axis_direction(mode, right);
    if (!started || sign == 0)
        return speed >= -tolerance && speed <= tolerance;
    // Check raw bounds before multiplication so even untrusted integer extremes cannot overflow.
    return speed >= -75 && speed <= 75 && speed * sign >= -tolerance;
}

/**
 * One explicitly selected signed wire window, bounded independently of the control loop.
 * Owner-thread only; caller supplies monotonic time. Any rejection or first zero
 * after movement permanently closes the window. This object grants no runtime authority.
 */
class MotionGate final {
  public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto default_window = std::chrono::milliseconds{3000};
    static constexpr auto maximum_window = std::chrono::milliseconds{8000};
    enum class End : std::uint8_t { none, zero_target, rejected, closed, time_limit };
    /**
     * Construct a disarmed owner-thread gate without borrowing any resources.
     * @param right Select high-half right targets, otherwise low-half left targets.
     * @param window Hard nonzero limit, 3000..8000ms; invalid values permanently inhibit motion.
     */
    explicit MotionGate(bool right, std::chrono::milliseconds window = default_window) noexcept
        : MotionGate(right ? MotionMode::right : MotionMode::left, window) {}
    /**
     * Construct a disarmed owner-only signed profile; borrows nothing.
     * @param mode Exact wheel relationship. Non-motion/invalid modes inhibit all nonzero writes.
     * @param right_sign Physical right-axis polarity, +1 or -1; raw targets are normalized before checking.
     * @param window New G3 modes require exactly 3000ms; legacy positive single-wheel modes retain 3000..8000ms.
     */
    explicit MotionGate(MotionMode mode, std::chrono::milliseconds window = default_window, int right_sign = 1) noexcept
        : mode_{mode}, window_{window}, right_sign_{right_sign} {
        if ((right_sign != 1 && right_sign != -1) || window < default_window || window > maximum_window
            || ((mode != MotionMode::left && mode != MotionMode::right) && window != default_window))
            close(End::rejected);
    }
    /** Permit one window only after the caller verifies neutral, zero-speed enable. */
    void arm() noexcept {
        armed_ = true;
    }
    /** Permanently inhibit nonzero frames; zero stop commands remain permitted. */
    void close(End reason = End::closed) noexcept {
        differential_since_ = {};
        if (!closed_)
            end_ = reason;
        closed_ = true;
    }
    /** Return the latched cause, or the currently reached automatic cutoff. */
    [[nodiscard]] End reason(Clock::time_point now) const noexcept {
        return closed_ ? end_ : done(now) ? End::time_limit : End::none;
    }
    /** Return whether a started window ended; stop early enough for the next 10ms cycle. */
    [[nodiscard]] bool done(Clock::time_point now) const noexcept {
        return started_ && (closed_ || now - first_ >= window_ - std::chrono::milliseconds{50});
    }
    /** Return whether a nonzero frame was admitted; no feedback success is implied. */
    [[nodiscard]] bool started() const noexcept {
        return started_;
    }
    /**
     * Return the start of the current admitted unequal-target interval, or the zero time point otherwise.
     * Owner-thread only; returns a value and borrows nothing. This does not prove successful send or feedback.
     */
    [[nodiscard]] Clock::time_point differential_since() const noexcept {
        return differential_since_;
    }
    /**
     * Check decoded wire targets before send. Zero is always allowed; nonzero
     * requires Enable Operation, the selected signed profile (each moving axis 1..5rpm) and time below the configured limit.
     * @return False latches rejection; never clamps or substitutes target bytes.
     */
    // Parameters follow decoded controlword/low-half/high-half wire order.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    [[nodiscard]] bool allow(int word, int left, int right, Clock::time_point now) noexcept {
        if (left == 0 && right == 0) {
            differential_since_ = {};
            if (started_)
                close(End::zero_target);
            return true;
        }
        if (!armed_ || closed_ || word != 15 || right < -5 || right > 5
            || !targets_match(mode_, left, right * right_sign_)
            || (started_ && (now < last_ || now - first_ >= window_))) {
            close(End::rejected);
            return false;
        }
        if (!started_)
            first_ = now;
        started_ = true;
        last_ = now;
        if (is_differential(mode_) && left != right * right_sign_) {
            if (differential_since_ == Clock::time_point{})
                differential_since_ = now;
        } else {
            differential_since_ = {};
        }
        return true;
    }

  private:
    End end_{End::none};
    MotionMode mode_;
    std::chrono::milliseconds window_;
    int right_sign_{1};
    bool armed_{false};
    bool closed_{false};
    bool started_{false};
    Clock::time_point first_{};
    Clock::time_point last_{};
    Clock::time_point differential_since_{};
};

/**
 * Bind the borrowed gate to the GNU send wrapper on the sole owner thread.
 * Null restores zero-only behavior. Caller must unbind before destroying the gate.
 * A thread-local pointer is necessary because the syscall ABI carries no context;
 * it is confined to this Debug-only tool, never shared with production/runtime.
 */
void bind_motion_gate(MotionGate* gate) noexcept;
} // namespace robot_control::hil
