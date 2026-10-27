#include <iostream>
#include <type_traits>
#include "app/motion_monitor.hpp"

/** Check continuous commands, transient bounds, stationary wheels and the exact stop deadline without hardware. */
int main() {
    using namespace std::chrono_literals;
    using robot_control::app::MotionMonitor;
    static_assert(!std::is_default_constructible_v<MotionMonitor>);
    const auto t = MotionMonitor::Clock::time_point{10s};
    int failed = 0;
    const auto check = [&](bool ok) {
        if (!ok)
            ++failed;
    };
    MotionMonitor boundary{20};
    boundary.start(t);
    boundary.observe(20, -20, t);
    boundary.observe(-20, 20, t + 150ms);
    check(boundary.ok(t + 150ms) && boundary.settled(t + 150ms));
    boundary.observe(21, 0, t + 160ms);
    check(boundary.ok(t + 160ms) && !boundary.settled(t + 160ms));
    boundary.command(5, 0, t + 170ms);
    boundary.observe(-20, 0, t + 370ms);
    check(boundary.ok(t + 370ms) && boundary.transients() == 0);
    boundary.observe(-21, 0, t + 371ms);
    check(boundary.fault() == MotionMonitor::Fault::direction);

    MotionMonitor configured{10};
    configured.start(t);
    configured.observe(10, -10, t);
    configured.observe(-10, 10, t + 150ms);
    check(configured.settled(t + 150ms));
    configured.observe(11, 0, t + 160ms);
    check(configured.ok(t + 160ms) && !configured.settled(t + 160ms));
    configured.command(5, 0, t + 170ms);
    configured.observe(-10, 0, t + 370ms);
    check(configured.ok(t + 370ms));
    configured.observe(-11, 0, t + 371ms);
    check(configured.fault() == MotionMonitor::Fault::direction);

    MotionMonitor normal{20};
    normal.start(t);
    normal.observe(0, 0, t);
    normal.command(5, 0, t);
    normal.observe(-29, 0, t + 10ms);
    check(normal.ok(t + 10ms) && normal.transients() == 1);
    normal.observe(50, 0, t + 210ms);
    check(normal.ok(t + 210ms));
    normal.command(0, 0, t + 220ms);
    normal.observe(-29, 0, t + 230ms);
    check(normal.ok(t + 230ms));
    normal.observe(0, 0, t + 300ms);
    normal.observe(0, 0, t + 450ms);
    check(normal.settled(t + 450ms));
    normal.command(-5, 5, t + 460ms);
    normal.observe(-50, 50, t + 700ms);
    check(normal.ok(t + 700ms));
    normal.shutdown(t + 710ms);
    normal.observe(0, 0, t + 720ms);
    normal.observe(0, 0, t + 870ms);
    check(normal.settled(t + 870ms));
    MotionMonitor idle{20};
    idle.start(t);
    idle.observe(0, 21, t);
    check(idle.ok(t) && !idle.settled(t));
    idle.observe(-50, 50, t + 50ms);
    idle.observe(50, -50, t + 100ms);
    check(idle.ok(t + 100ms) && !idle.settled(t + 100ms));
    idle.observe(-13, -21, t + 150ms); // run-5475 idle feedback must not veto the owner.
    check(idle.safe(t + 150ms));
    for (unsigned axis = 0; axis < 2; ++axis) {
        for (const int value : {-51, 51}) {
            MotionMonitor outside{20};
            outside.start(t);
            outside.observe(axis == 0 ? value : 0, axis == 1 ? value : 0, t);
            check(outside.fault() == MotionMonitor::Fault::uncommanded && !outside.safe(t));
        }
    }
    MotionMonitor hard{20};
    hard.start(t);
    hard.command(5, 0, t);
    hard.observe(1001, 0, t + 1ms);
    check(!hard.ok(t + 1ms));
    MotionMonitor wrong{20};
    wrong.start(t);
    wrong.command(-5, 0, t);
    wrong.observe(21, 0, t + 200ms);
    check(!wrong.ok(t + 200ms));
    MotionMonitor timeout{20};
    timeout.start(t);
    timeout.command(5, 0, t);
    timeout.observe(50, 0, t);
    timeout.command(0, 0, t + 1ms);
    for (int i = 1; i <= 10; ++i)
        timeout.observe(21, 0, t + 1ms + i * 100ms);
    check(!timeout.ok(t + 1001ms));
    MotionMonitor stale{20};
    stale.start(t);
    stale.observe(0, 0, t);
    check(!stale.ok(t + 200ms));
    MotionMonitor repeat{20};
    repeat.start(t);
    repeat.shutdown(t);
    repeat.observe(0, 0, t);
    check(!repeat.settled(t + 150ms));
    repeat.observe(0, 0, t + 150ms);
    check(repeat.settled(t + 150ms));
    MotionMonitor late{20};
    late.start(t);
    late.command(5, 0, t);
    late.command(0, 0, t + 1ms);
    late.observe(0, 0, t + 900ms);
    late.observe(0, 0, t + 1060ms);
    check(!late.safe(t + 1060ms)); // A late stable hold cannot erase the one-second deadline.
    MotionMonitor extremes{20};
    extremes.start(t);
    extremes.observe(1, -1, t);
    check(extremes.minimum(0) == 1 && extremes.maximum(1) == -1);
    // Reproduce run-4769: an early stable hold followed by a 2.1rpm stop rebound.
    MotionMonitor rebound{20};
    rebound.start(t);
    rebound.command(-9, -9, t);
    rebound.command(0, 0, t + 1ms);
    rebound.observe(-13, -12, t + 100ms);
    rebound.observe(-7, -10, t + 150ms);
    rebound.observe(0, 0, t + 200ms);
    rebound.observe(13, 12, t + 250ms);
    check(rebound.settled(t + 250ms));
    rebound.observe(21, 19, t + 300ms);
    check(rebound.ok(t + 300ms) && !rebound.settled(t + 300ms));
    rebound.observe(0, 0, t + 350ms);
    rebound.observe(0, 0, t + 500ms);
    check(rebound.settled(t + 500ms));
    for (int i = 6; i <= 10; ++i)
        rebound.observe(0, 0, t + i * 100ms);
    check(rebound.ok(t + 1001ms));
    rebound.observe(21, 0, t + 1050ms);
    check(rebound.ok(t + 1050ms) && !rebound.settled(t + 1050ms));
    rebound.observe(51, 0, t + 1100ms);
    check(rebound.fault() == MotionMonitor::Fault::uncommanded);
    MotionMonitor unsafe_stop{20};
    unsafe_stop.start(t);
    unsafe_stop.command(5, 0, t);
    unsafe_stop.command(0, 0, t + 1ms);
    unsafe_stop.observe(1001, 0, t + 2ms);
    check(!unsafe_stop.ok(t + 2ms));
    if (failed)
        std::cerr << failed << " motion monitor failures\n";
    return failed ? 1 : 0;
}
