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
    std::cout << "motion_gate_failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
