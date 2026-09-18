#include "tools/control_hil/trace.hpp"

#include <iostream>
#include <sstream>

using namespace std::chrono_literals;
using robot_control::hil::Trace;

/** Check bounded capture, exact signed feedback, and terminal failure without any devices. */
int main() {
    unsigned failures = 0;
    const auto check = [&](bool value) {
        failures += value ? 0U : 1U;
    };
    const auto now = Trace::Clock::time_point{1s};
    {
        Trace trace{Trace::Mode::left, 2};
        trace.append(Trace::Kind::stop, now, {});
        trace.append(Trace::Kind::stop, now, {});
        check(!trace.failed());
        trace.append(Trace::Kind::stop, now, {});
        check(trace.failed());
        std::ostringstream output;
        check(trace.dump(output));
        check(output.str().find("count=2 overflow=1") != std::string::npos);
    }
    // Prospective zero-only tolerance; raw signed endpoints are retained and failures latch.
    for (int value : {-32768, -11, -10, -1, 0, 1, 10, 11, 32767}) {
        for (unsigned offset : {4U, 6U}) {
            Trace trace{Trace::Mode::zero, 8, 10};
            trace.watch_feedback();
            can_frame rx{};
            rx.can_id = 0x181;
            rx.can_dlc = 8;
            const auto encoded = static_cast<unsigned>(value) & 0xffffU;
            rx.data[offset] = static_cast<unsigned char>(encoded & 255U);
            rx.data[offset + 1] = static_cast<unsigned char>(encoded >> 8U);
            trace.can(Trace::Kind::can_rx, rx, now, CAN_MTU, 0);
            check(trace.failed() == (value < -10 || value > 10));
            rx.data[offset] = rx.data[offset + 1] = 0;
            trace.can(Trace::Kind::can_rx, rx, now + 10ms, CAN_MTU, 0);
            check(trace.failed() == (value < -10 || value > 10));
        }
    }
    check(Trace{Trace::Mode::zero, 8, 21}.failed());
    check(!Trace{Trace::Mode::left, 8, 15}.failed());
    check(!Trace{Trace::Mode::right, 8, 15}.failed());
    check(Trace{Trace::Mode::input_only, 8, 10}.failed());
    for (int tolerance : {15, 20}) {
        for (const auto mode : {Trace::Mode::left, Trace::Mode::right}) {
            for (int stage : {0, 1, 2}) {
                for (int speed : {-tolerance-1, -tolerance, -1, 0, tolerance, tolerance+1}) {
                    Trace trace{mode, 16, tolerance};
                    trace.watch_feedback();
                    can_frame tx{};
                    tx.can_id = 0x201;
                    tx.can_dlc = 6;
                    tx.data[0] = 15;
                    tx.data[mode == Trace::Mode::left ? 2 : 4] = 5;
                    if (stage > 0)
                        trace.can(Trace::Kind::can_tx, tx, now, CAN_MTU, 0);
                    if (stage == 2) {
                        tx.data[2] = tx.data[4] = 0;
                        trace.can(Trace::Kind::can_tx, tx, now + 1ms, CAN_MTU, 0);
                    }
                    can_frame rx{};
                    rx.can_id = 0x181;
                    rx.can_dlc = 8;
                    const unsigned offset = mode == Trace::Mode::left ? 4U : 6U;
                    const auto encoded = static_cast<unsigned>(speed) & 0xffffU;
                    rx.data[offset] = static_cast<unsigned char>(encoded & 255U);
                    rx.data[offset + 1] = static_cast<unsigned char>(encoded >> 8U);
                    trace.can(Trace::Kind::can_rx, rx, now + 2ms, CAN_MTU, 0);
                    check(trace.failed() == (stage == 0 ? (speed < -tolerance || speed > tolerance) : stage == 1 && speed < -tolerance));
                }
            }
        }
    }
    for (const auto mode : {Trace::Mode::left, Trace::Mode::right}) {
        Trace trace{mode};
        trace.watch_feedback();
        can_frame tx{};
        tx.can_id = 0x201;
        tx.can_dlc = 6;
        tx.data[0] = 15;
        tx.data[mode == Trace::Mode::left ? 2 : 4] = 5;
        trace.can(Trace::Kind::can_tx, tx, now, CAN_MTU, 0);
        can_frame rx{};
        rx.can_id = 0x181;
        rx.can_dlc = 8;
        const unsigned offset = mode == Trace::Mode::left ? 4U : 6U;
        rx.data[offset] = 50;
        trace.can(Trace::Kind::can_rx, rx, now, CAN_MTU, 0);
        check(!trace.failed());
        tx.data[2] = tx.data[4] = 0;
        tx.data[0] = 6;
        trace.can(Trace::Kind::can_tx, tx, now + 1s, CAN_MTU, 0);
        // Stop-band excursions remain recorded for review without failing capture.
        for (int value : {-10, -36, 9, -5, 0, 3, 0, 0, -6, -1, 0, 0, 0}) {
            const auto encoded = static_cast<unsigned>(value) & 0xffffU;
            rx.data[offset] = static_cast<unsigned char>(encoded & 255U);
            rx.data[offset + 1] = static_cast<unsigned char>(encoded >> 8U);
            trace.can(Trace::Kind::can_rx, rx, now + 1100ms, CAN_MTU, 0);
            check(!trace.failed());
        }
        std::ostringstream output;
        check(trace.dump(output));
        check(output.str().find("feedback_bad=1") != std::string::npos);
        check(output.str().find("event=feedback_review ") != std::string::npos);
        rx.data[offset] = 0xb4; // -7.6rpm must still latch a hard failure.
        rx.data[offset + 1] = 0xff;
        trace.can(Trace::Kind::can_rx, rx, now + 1200ms, CAN_MTU, 0);
        check(trace.failed());
    }
    {
        Trace trace{Trace::Mode::left};
        robot_control::input::sbus::ReadBatch batch;
        batch.captured_at = now;
        batch.session = 7;
        batch.event_count = 2;
        batch.events[0].kind = batch.events[1].kind = robot_control::input::sbus::protocol::EventKind::frame;
        batch.events[0].frame.channels[0] = 1200;
        batch.events[1].frame.channels[0] = 800;
        trace.batch(batch);
        std::ostringstream output;
        check(trace.dump(output));
        check(output.str().find("count=3 overflow=0") != std::string::npos);
        check(output.str().find("1200") != std::string::npos && output.str().find("800") != std::string::npos);
        batch.event_count = batch.events.size() + 1;
        trace.batch(batch);
        check(trace.failed());
    }
    {
        int descriptors[2]{};
        check(::pipe2(descriptors, O_NONBLOCK | O_CLOEXEC) == 0);
        Trace trace{Trace::Mode::zero, 1, 20, descriptors[1]};
        Trace::Packet packet{};
        check(::read(descriptors[0], packet.data(), sizeof(packet)) == sizeof(packet));
        check(packet[0] == 0 && packet[1] == 10 && packet[3] == 1 && packet[4] == 20);
        // Streaming has bounded pipe capacity, not the legacy in-memory count limit.
        for (int i = 0; i < 100; ++i) {
            trace.append(Trace::Kind::cycle, now, {i});
            check(::read(descriptors[0], packet.data(), sizeof(packet)) == sizeof(packet));
            check(packet[0] == i + 1 && packet[3] == i);
        }
        check(!trace.failed());
        const auto begin = Trace::Clock::now();
        for (int i = 0; i < 10000 && !trace.failed(); ++i)
            trace.append(Trace::Kind::cycle, now, {});
        check(trace.failed() && Trace::Clock::now() - begin < 100ms);
        ::close(descriptors[0]);
        ::close(descriptors[1]);
    }
    std::cout << "trace_failures=" << failures << '\n';
    return failures ? 1 : 0;
}
