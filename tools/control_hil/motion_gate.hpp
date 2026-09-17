#pragma once

#include <chrono>
#include <cstdint>

namespace robot_control::hil {
/**
 * One positive single-wheel wire window, bounded independently of the control loop.
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
        : right_{right}, window_{window} {
        if (window < default_window || window > maximum_window)
            close(End::rejected);
    }
    /** Permit one window only after the caller verifies neutral, zero-speed enable. */
    void arm() noexcept {
        armed_ = true;
    }
    /** Permanently inhibit nonzero frames; zero stop commands remain permitted. */
    void close(End reason = End::closed) noexcept {
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
     * Check decoded wire targets before send. Zero is always allowed; nonzero
     * requires Enable Operation, selected axis 1..5rpm, opposite zero and time below the configured limit.
     * @return False latches rejection; never clamps or substitutes target bytes.
     */
    // Parameters follow decoded controlword/low-half/high-half wire order.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    [[nodiscard]] bool allow(int word, int left, int right, Clock::time_point now) noexcept {
        if (left == 0 && right == 0) {
            if (started_)
                close(End::zero_target);
            return true;
        }
        const int selected = right_ ? right : left;
        const int other = right_ ? left : right;
        if (!armed_ || closed_ || word != 15 || selected < 1 || selected > 5 || other != 0
            || (started_ && (now < last_ || now - first_ >= window_))) {
            close(End::rejected);
            return false;
        }
        if (!started_)
            first_ = now;
        started_ = true;
        last_ = now;
        return true;
    }

  private:
    End end_{End::none};
    bool right_;
    std::chrono::milliseconds window_;
    bool armed_{false};
    bool closed_{false};
    bool started_{false};
    Clock::time_point first_{};
    Clock::time_point last_{};
};

/**
 * Bind the borrowed gate to the GNU send wrapper on the sole owner thread.
 * Null restores zero-only behavior. Caller must unbind before destroying the gate.
 * A thread-local pointer is necessary because the syscall ABI carries no context;
 * it is confined to this Debug-only tool, never shared with production/runtime.
 */
void bind_motion_gate(MotionGate* gate) noexcept;
} // namespace robot_control::hil
