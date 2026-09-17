#include "tools/control_hil/motion_gate.hpp"

#include <cerrno>
#include <cstdint>
#include <iostream>
#include <linux/can.h>
#include <sys/socket.h>

extern "C" ssize_t __wrap_send(int, const void*, size_t, int);
/** Never touch a socket; only accepted frames reach this test syscall stub. */
extern "C" ssize_t __real_send(int, const void*, size_t size, int) {
    return static_cast<ssize_t>(size);
}
/** Check actual wrapper behavior for every target byte, flag, controlword and SDO width. */
int main() {
    int failures = 0;
    const auto check = [&](can_frame frame, bool expected) {
        errno = 0;
        const auto sent = __wrap_send(-1, &frame, CAN_MTU, MSG_DONTWAIT);
        if ((sent == CAN_MTU) != expected || (!expected && errno != EACCES))
            ++failures;
    };
    can_frame rpdo{};
    rpdo.can_id = 0x201;
    rpdo.can_dlc = 6;
    for (unsigned word = 0; word < 256; ++word) {
        rpdo.data[0] = static_cast<std::uint8_t>(word);
        check(rpdo, word == 0 || word == 2 || word == 6 || word == 7 || word == 15);
    }
    rpdo.data[0] = 15;
    for (unsigned byte = 1; byte < 6; ++byte)
        for (unsigned value = 1; value < 256; ++value) {
            auto bad = rpdo;
            bad.data[byte] = static_cast<std::uint8_t>(value);
            check(bad, false);
        }
    for (auto flag : {CAN_EFF_FLAG, CAN_RTR_FLAG, CAN_ERR_FLAG}) {
        auto bad = rpdo;
        bad.can_id |= flag;
        check(bad, false);
    }
    for (unsigned sub = 0; sub <= 4; ++sub) {
        can_frame sdo{};
        sdo.can_id = 0x601;
        sdo.can_dlc = 8;
        sdo.data[0] = 0x23;
        sdo.data[1] = 0xff;
        sdo.data[2] = 0x60;
        sdo.data[3] = static_cast<std::uint8_t>(sub);
        check(sdo, sub == 1 || sub == 2);
        for (unsigned byte = 4; byte < 8; ++byte) {
            auto bad = sdo;
            bad.data[byte] = 1;
            check(bad, false);
        }
        for (auto command : {0x2f, 0x2b, 0x27}) {
            sdo.data[0] = static_cast<std::uint8_t>(command);
            check(sdo, false);
        }
    }
    // Verify the actual wire decoder and borrowed-context lifetime, not only the pure predicate.
    for (bool right : {false, true}) {
        robot_control::hil::MotionGate gate{right};
        robot_control::hil::bind_motion_gate(&gate);
        gate.arm();
        auto single = rpdo;
        single.data[right ? 4 : 2] = 5;
        check(single, true);
        auto both = single;
        both.data[right ? 2 : 4] = 1;
        check(both, false);
        check(single, false); // Terminal rejection.
        check(rpdo, true);    // Stop remains possible.
        robot_control::hil::bind_motion_gate(nullptr);
        check(single, false); // No borrowed context means zero-only.
    }
    if (__wrap_send(-1, nullptr, CAN_MTU, MSG_DONTWAIT) != -1)
        ++failures;
    if (__wrap_send(-1, &rpdo, CAN_MTU - 1, MSG_DONTWAIT) != -1)
        ++failures;
    if (__wrap_send(-1, &rpdo, CAN_MTU, 0) != -1)
        ++failures;
    std::cout << "zero_gate_failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}

/** Link-time receive stub for send-only gate tests; no real socket is accessed. */
extern "C" ssize_t __real_recv(int, void*, size_t, int) {
    errno = EAGAIN;
    return -1;
}
