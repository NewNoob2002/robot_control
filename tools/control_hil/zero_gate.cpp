#include "communication/canopen/control_write_gate.hpp"
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
    if (f.can_id == 0x201) {
        if (f.can_dlc != 6 || f.data[1] != 0)
            return false;
        if (f.data[0] != 0 && f.data[0] != 2 && f.data[0] != 6 && f.data[0] != 7 && f.data[0] != 15)
            return false;
        /** Decode signed little-endian targets without implementation-defined casts. */
        const auto decode = [&](unsigned offset) {
            const int raw = f.data[offset] | (static_cast<int>(f.data[offset + 1]) << 8);
            return raw >= 32768 ? raw - 65536 : raw;
        };
        const int left = decode(2), right = decode(4);
        return motion_gate ? motion_gate->allow(f.data[0], left, right, robot_control::hil::MotionGate::Clock::now())
                           : left == 0 && right == 0;
    }
    return robot_control::communication::canopen::control_setup_allowed(f);
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
