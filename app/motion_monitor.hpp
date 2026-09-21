#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>

namespace robot_control::app {
/** Additional test-app feedback envelope; single owner, pure/injected clock, no CAN authority. */
class MotionMonitor final {
  public:
    using Clock = std::chrono::steady_clock;
    enum class Fault { none, hard_speed, uncommanded, direction, stop_timeout, stale };
    /** Begin after actual zero/preflight verification; never grants drive authorization. */
    void start(Clock::time_point now) noexcept {
        watching_ = true;
        last_ = now;
    }
    /** Record only a successfully sent signed logical target pair; owner only, retains values. */
    void command(int left, int right, Clock::time_point now) noexcept {
        const std::array values{left, right};
        for (unsigned i = 0; i < 2; ++i) {
            auto& a = axes_[i];
            if (values[i] == 0 && a.target != 0) {
                a.stopping = true;
                a.since = now;
                a.stable = {};
            } else if (values[i] != 0 && (a.target == 0 || (values[i] > 0) != (a.target > 0))) {
                a.stopping = false;
                a.since = now;
                a.stable = {};
            }
            a.target = values[i];
        }
    }
    /** Start a fresh bounded standstill hold after the caller has inhibited and sent zero. */
    void shutdown(Clock::time_point now) noexcept {
        for (auto& a : axes_) {
            a.target = 0;
            a.stopping = true;
            a.since = now;
            a.stable = {};
        }
    }
    /** Observe every raw feedback event normalized by the fixed axis polarity; owner only. */
    void observe(int left, int right, Clock::time_point now) noexcept {
        if (!watching_)
            return;
        const auto previous = last_;
        last_ = now;
        const std::array values{left, right};
        if (!have_feedback_) {
            minimum_ = maximum_ = values;
            have_feedback_ = true;
        }
        for (unsigned i = 0; i < 2; ++i) {
            auto& a = axes_[i];
            const int v = values[i];
            // Keep the original one-second stop window even after an early stable hold.
            // A later rebound restarts the hold, never the deadline.
            if (a.stopping && now - a.since >= std::chrono::seconds{1}) {
                if (!stable(a, previous))
                    fail(Fault::stop_timeout);
                a.stopping = false;
            }
            minimum_[i] = std::min(minimum_[i], v);
            maximum_[i] = std::max(maximum_[i], v);
            const bool band = v >= -20 && v <= 20;
            if (v < -1000 || v > 1000)
                fail(Fault::hard_speed);
            if (a.target != 0) {
                const bool wrong = a.target > 0 ? v < -20 : v > 20;
                if (wrong && now - a.since >= std::chrono::milliseconds{200})
                    fail(Fault::direction);
                else if (wrong)
                    ++transients_;
            } else if (a.stopping) {
                if (!band) {
                    a.stable = {};
                    ++transients_;
                } else if (a.stable == Clock::time_point{})
                    a.stable = now;
            } else if (!band) {
                a.stable = {};
                if (v < -50 || v > 50)
                    fail(Fault::uncommanded);
            } else if (a.stable == Clock::time_point{})
                a.stable = now;
        }
    }
    /** Check bounded convergence/freshness without refreshing a feedback timestamp. Failure latches. */
    [[nodiscard]] bool ok(Clock::time_point now) noexcept {
        if (watching_) {
            if (now < last_ || now - last_ >= std::chrono::milliseconds{200})
                fail(Fault::stale);
            for (const auto& a : axes_)
                if (a.stopping && now - a.since >= std::chrono::seconds{1} && !stable(a, last_))
                    fail(Fault::stop_timeout);
        }
        return fault_ == Fault::none;
    }
    /** Pure owner-local veto for the control loop, after new events and before command publication. */
    [[nodiscard]] bool safe(Clock::time_point now) const noexcept {
        if (fault_ != Fault::none)
            return false;
        if (!watching_)
            return true;
        if (now < last_ || now - last_ >= std::chrono::milliseconds{200})
            return false;
        for (const auto& a : axes_)
            if (a.stopping && now - a.since >= std::chrono::seconds{1} && !stable(a, last_))
                return false;
        return true;
    }
    /** Require both zero targets and two fresh frames spanning at least150ms inside the +/-2rpm band. */
    [[nodiscard]] bool settled(Clock::time_point now) const noexcept {
        if (!watching_ || now < last_ || now - last_ >= std::chrono::milliseconds{200})
            return false;
        for (const auto& a : axes_)
            if (a.target != 0 || !stable(a, last_))
                return false;
        return true;
    }
    /** Return the first latched failure for diagnostics; no ownership transfer. */
    [[nodiscard]] Fault fault() const noexcept {
        return fault_;
    }
    /** Return raw transient sample count; count never changes the recorded feedback values. */
    [[nodiscard]] std::uint64_t transients() const noexcept {
        return transients_;
    }
    /** Return per-axis phase at this observation:0 idle,1 startup,2 commanded,3 stopping; diagnostic only. */
    [[nodiscard]] int phase(unsigned axis, Clock::time_point now) const noexcept {
        const auto& a = axes_[axis];
        return a.stopping ? 3 : a.target == 0 ? 0 : now - a.since < std::chrono::milliseconds{200} ? 1 : 2;
    }
    /** Return lifetime signed extremes without filtering samples; diagnostic only, axis must be0 or1. */
    [[nodiscard]] int minimum(unsigned axis) const noexcept {
        return minimum_[axis];
    }
    /** Return lifetime signed extremes without filtering samples; diagnostic only, axis must be0 or1. */
    [[nodiscard]] int maximum(unsigned axis) const noexcept {
        return maximum_[axis];
    }

  private:
    struct Axis {
        int target{0};
        bool stopping{false};
        Clock::time_point since{}, stable{};
    };
    /** Require received feedback spanning the full standstill hold; elapsed wall time is insufficient. */
    [[nodiscard]] static bool stable(const Axis& axis, Clock::time_point sample) noexcept {
        return axis.stable != Clock::time_point{} && sample - axis.stable >= std::chrono::milliseconds{150};
    }
    /** Preserve the original protection reason. */
    void fail(Fault fault) noexcept {
        if (fault_ == Fault::none)
            fault_ = fault;
    }
    std::array<Axis, 2> axes_{};
    std::array<int, 2> minimum_{}, maximum_{};
    Clock::time_point last_{};
    Fault fault_{Fault::none};
    bool watching_{false};
    bool have_feedback_{false};
    std::uint64_t transients_{0};
};
} // namespace robot_control::app
