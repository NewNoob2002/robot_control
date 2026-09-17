#include "communication/canopen/qualification_gate.h"
#include "tools/control_hil/motion_gate.hpp"
#include "tools/control_hil/trace.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <linux/can.h>
#include <sys/socket.h>

namespace {
// See bind_motion_gate: explicit borrowed context for the fixed GNU syscall ABI.
thread_local robot_control::hil::MotionGate* motion_gate = nullptr;
thread_local robot_control::hil::Trace* trace = nullptr;
/** Independently constrain node-1 HIL writes; SDO remains zero-only in every mode. */
bool allowed(const can_frame& f) noexcept {
    if (trace && trace->input_only())
        return false;
    if (f.can_id == 0)
        return f.can_dlc == 2 && f.data[1] == 1 && (f.data[0] == 1 || f.data[0] == 0x80);
    if (f.can_id == 0x201) {
        if (f.can_dlc != 6 || f.data[1] != 0)
            return false;
        if (f.data[0] != 0 && f.data[0] != 2 && f.data[0] != 6 && f.data[0] != 7 && f.data[0] != 15)
            return false;
        const int left = f.data[2] | (static_cast<int>(f.data[3]) << 8);
        const int right = f.data[4] | (static_cast<int>(f.data[5]) << 8);
        return motion_gate ? motion_gate->allow(f.data[0], left, right, robot_control::hil::MotionGate::Clock::now())
                           : left == 0 && right == 0;
    }
    if (f.can_id != 0x601 || f.can_dlc != 8)
        return false;
    const auto index = static_cast<std::uint16_t>(f.data[1] | (static_cast<unsigned>(f.data[2]) << 8U));
    const auto sub = f.data[3];
    const auto width = robot_control_canopen_qualification_upload_size({index, sub});
    if (width == 0)
        return false;
    if (f.data[0] == 0x40) {
        for (unsigned i = 4; i < 8; ++i)
            if (f.data[i] != 0)
                return false;
        return true;
    }
    if (f.data[0] != (width == 1 ? 0x2f : width == 2 ? 0x2b : 0x23))
        return false;
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(f.data[4 + i]) << (8U * i);
    switch (index) {
        case 0x6040:
            return sub == 0 && (value == 0 || value == 6);
        case 0x60ff:
            return (sub == 1 || sub == 2) && value == 0;
        case 0x1017:
            return sub == 0 && (value == 0 || value == 500);
        case 0x2000:
            return sub == 0 && (value == 0 || value == 1000);
        case 0x1400:
            return sub == 1 && (value == 0x201 || value == 0x80000201);
        case 0x1801:
            return (sub == 1 && (value == 0x281 || value == 0x80000281)) || (sub == 5 && (value == 0 || value == 100));
        case 0x1600:
            return (sub == 0 && (value == 0 || value == 2)) || (sub == 1 && value == 0x60400010)
                   || (sub == 2 && (value == 0x60600008 || value == 0x60ff0320));
        case 0x1a01:
            return (sub == 0 && (value == 0 || value == 2)) || (sub == 1 && (value == 0 || value == 0x60610008))
                   || (sub == 2 && (value == 0 || value == 0x603f0020));
        default:
            return false;
    }
}
} // namespace

void robot_control::hil::bind_motion_gate(MotionGate* gate) noexcept {
    motion_gate = gate;
}

void robot_control::hil::bind_trace(Trace* value) noexcept {
    trace = value;
}

// GNU ld --wrap requires these exact external symbol names.
// NOLINTNEXTLINE(bugprone-reserved-identifier)
extern "C" ssize_t __real_send(int, const void*, size_t, int);
/** Reject unreviewed frames before the kernel; never clamp, retry or substitute bytes. */
// NOLINTNEXTLINE(bugprone-reserved-identifier)
extern "C" ssize_t __wrap_send(int fd, const void* buffer, size_t length, int flags) {
    can_frame frame{};
    if (buffer == nullptr || length != sizeof(frame) || flags != MSG_DONTWAIT) {
        errno = EACCES;
        return -1;
    }
    std::memcpy(&frame, buffer, sizeof(frame));
    if (!allowed(frame)) {
        if (trace)
            trace->can(robot_control::hil::Trace::Kind::can_tx, frame, robot_control::hil::Trace::Clock::now(), -1,
                       EACCES);
        errno = EACCES;
        return -1;
    }
    const auto result = __real_send(fd, buffer, length, flags);
    const int error = errno;
    if (trace)
        trace->can(robot_control::hil::Trace::Kind::can_tx, frame, robot_control::hil::Trace::Clock::now(), result,
                   result < 0 ? error : 0);
    errno = error;
    return result;
}

// NOLINTNEXTLINE(bugprone-reserved-identifier)
extern "C" ssize_t __real_recv(int, void*, size_t, int);
/** Record the owner's successful CAN peek without consuming extra frames or altering errno/result. */
// NOLINTNEXTLINE(bugprone-reserved-identifier)
extern "C" ssize_t __wrap_recv(int fd, void* buffer, size_t length, int flags) {
    const auto result = __real_recv(fd, buffer, length, flags);
    const int error = errno;
    if (trace && result == CAN_MTU && length == CAN_MTU && flags == (MSG_PEEK | MSG_DONTWAIT)) {
        can_frame frame{};
        std::memcpy(&frame, buffer, sizeof(frame));
        trace->can(robot_control::hil::Trace::Kind::can_rx, frame, robot_control::hil::Trace::Clock::now(), result, 0);
    }
    errno = error;
    return result;
}
