#include "tools/control_hil/motion_gate.hpp"

#include <iostream>
#include <utility>

using namespace std::chrono_literals;
using robot_control::hil::MotionGate;

/** Exercise the wire envelope with injected monotonic time; no hardware or sleeping. */
int main() {
    int failures = 0;
    const auto check = [&](bool result) {
        failures += result ? 0 : 1;
    };
    const auto now = MotionGate::Clock::time_point{1s};
    for (bool right : {false, true}) {
        const auto left = right ? 0 : 5;
        const auto other = right ? 5 : 0;
        {
            MotionGate gate{right};
            check(!gate.allow(15, left, other, now)); // Before verified zero enable.
            gate.arm();
            check(!gate.allow(15, left, other, now)); // Rejection is terminal.
            check(gate.allow(0, 0, 0, now));
        }
        {
            MotionGate gate{right};
            gate.arm();
            check(gate.allow(15, left, other, now));
            check(gate.reason(now + 2950ms) == MotionGate::End::time_limit);
            check(gate.allow(15, left, other, now + 2999ms));
            check(!gate.allow(15, left, other, now + 3000ms));
            check(gate.reason(now + 3000ms) == MotionGate::End::rejected);
            check(gate.allow(2, 0, 0, now + 3s));
            check(!gate.allow(15, left, other, now + 4s));
        }
        {
            MotionGate gate{right};
            gate.arm();
            check(gate.allow(15, left, other, now));
            check(gate.allow(15, 0, 0, now + 10ms));
            check(gate.done(now + 10ms));
            check(gate.reason(now + 10ms) == MotionGate::End::zero_target);
            check(!gate.allow(15, left, other, now + 20ms));
        }
        for (auto word : {0, 2, 6, 7}) {
            MotionGate gate{right};
            gate.arm();
            check(!gate.allow(word, left, other, now));
        }
        for (auto pair : {std::pair{5, 5}, std::pair{-1, 0}, std::pair{0, -1}, std::pair{6, 0}, std::pair{0, 6},
                          std::pair{other, left}}) {
            MotionGate gate{right};
            gate.arm();
            check(!gate.allow(15, pair.first, pair.second, now));
        }
        {
            MotionGate gate{right};
            gate.arm();
            check(gate.allow(15, left, other, now));
            check(!gate.allow(15, left, other, now - 1ms));
        }
        {
            MotionGate gate{right, 8s};
            gate.arm();
            check(gate.allow(15, left, other, now));
            check(!gate.done(now + 3s));
            check(gate.allow(15, left, other, now + 7949ms));
            check(!gate.done(now + 7949ms));
            check(gate.reason(now + 7950ms) == MotionGate::End::time_limit);
            check(gate.allow(15, left, other, now + 7999ms));
            check(!gate.allow(15, left, other, now + 8s));
            check(gate.allow(2, 0, 0, now + 8s));
        }
        {
            MotionGate gate{right, 8s};
            gate.arm();
            check(gate.allow(15, left, other, now));
            check(gate.allow(2, 0, 0, now + 4s));
            gate.arm();
            check(!gate.allow(15, left, other, now + 4100ms));
        }
        for (const auto window : {-1ms, 0ms, 2999ms, 8001ms}) {
            MotionGate gate{right, window};
            gate.arm();
            check(!gate.allow(15, left, other, now));
            check(gate.allow(0, 0, 0, now));
        }
    }
    // Independent vectors lock each new mode to one signed wheel relationship.
    using Mode = robot_control::hil::MotionMode;
    struct Vector {
        Mode mode;
        int left;
        int right;
    };
    for (const auto vector :
         {Vector{Mode::left_negative, -5, 0}, Vector{Mode::right_negative, 0, -5}, Vector{Mode::dual_positive, 5, 5},
          Vector{Mode::dual_negative, -5, -5}, Vector{Mode::turn_left, -5, 5}, Vector{Mode::turn_right, 5, -5},
          Vector{Mode::differential_left, 3, 5}, Vector{Mode::differential_right, 5, 3}}) {
        for (int left = -6; left <= 6; ++left) {
            for (int right = -6; right <= 6; ++right) {
                /** Independent signed boundary reference, without production predicates. */
                const auto same_sign = [](int value, int reference) {
                    return reference == 0  ? value == 0
                           : reference < 0 ? value >= -5 && value <= -1
                                           : value >= 1 && value <= 5;
                };
                bool expected = same_sign(left, vector.left) && same_sign(right, vector.right);
                if (vector.mode == Mode::dual_positive || vector.mode == Mode::dual_negative)
                    expected = expected && left == right;
                if (vector.mode == Mode::turn_left || vector.mode == Mode::turn_right)
                    expected = expected && left == -right;
                if (vector.mode == Mode::differential_left)
                    expected = expected && left <= right;
                if (vector.mode == Mode::differential_right)
                    expected = expected && left >= right;
                for (int right_sign : {-1, 1}) {
                    MotionGate gate{vector.mode, 3s, right_sign};
                    gate.arm();
                    check(gate.allow(15, left, right * right_sign, now) == (expected || (left == 0 && right == 0)));
                }
            }
        }
        MotionGate gate{vector.mode};
        gate.arm();
        check(gate.allow(15, vector.left, vector.right, now));
        check(!gate.allow(15, vector.left, vector.right, now + 3s));
        check(gate.allow(2, 0, 0, now + 3s));
        gate.arm();
        check(!gate.allow(15, vector.left, vector.right, now + 4s));
        MotionGate extended{vector.mode, 8s};
        extended.arm();
        check(!extended.allow(15, vector.left, vector.right, now));
        MotionGate reversed_clock{vector.mode};
        reversed_clock.arm();
        check(reversed_clock.allow(15, vector.left, vector.right, now));
        check(!reversed_clock.allow(15, vector.left, vector.right, now - 1ms));
    }
    for (const auto mode : {Mode::differential_left, Mode::differential_right}) {
        const int left = mode == Mode::differential_left ? 4 : 5;
        const int right = mode == Mode::differential_left ? 5 : 4;
        for (int sign : {-1, 1}) {
            MotionGate gate{mode, 3s, sign};
            gate.arm();
            check(gate.allow(15, 3, 3 * sign, now));
            check(gate.differential_since() == MotionGate::Clock::time_point{});
            check(gate.allow(15, 5, 5 * sign, now + 500ms));
            check(gate.allow(15, left, right * sign, now + 2900ms));
            check(gate.differential_since() == now + 2900ms);
            check(gate.reason(now + 2950ms) == MotionGate::End::time_limit);
            check(!gate.allow(15, left, right * sign, now + 3s));
            check(gate.differential_since() == MotionGate::Clock::time_point{});
            check(gate.allow(2, 0, 0, now + 3s));
            gate.arm();
            check(!gate.allow(15, left, right * sign, now + 3100ms));
            MotionGate zero_closed{mode, 3s, sign};
            zero_closed.arm();
            check(zero_closed.allow(15, 5, 5 * sign, now));
            check(zero_closed.allow(2, 0, 0, now + 10ms));
            check(!zero_closed.allow(15, left, right * sign, now + 20ms));
            MotionGate interrupted{mode, 3s, sign};
            interrupted.arm();
            check(interrupted.allow(15, left, right * sign, now));
            check(interrupted.allow(15, 5, 5 * sign, now + 10ms));
            check(interrupted.differential_since() == MotionGate::Clock::time_point{});
            check(interrupted.allow(15, left, right * sign, now + 20ms));
            check(interrupted.differential_since() == now + 20ms);
        }
    }
    for (int invalid_sign : {0, -2, 2}) {
        MotionGate gate{Mode::dual_positive, 3s, invalid_sign};
        gate.arm();
        check(!gate.allow(15, 5, -5, now));
        check(gate.allow(0, 0, 0, now));
    }
    std::cout << "motion_gate_failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
