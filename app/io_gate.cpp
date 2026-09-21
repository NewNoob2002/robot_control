#include "app/io_gate.hpp"
#include <cstring>
#include <sys/socket.h>
#include "communication/canopen/control_write_gate.hpp"

namespace {
// GNU --wrap has a fixed C ABI. Explicit binding owns this borrowed, single-thread context;
// no process-lifetime policy singleton is used. Tested through actual vcan sends/receives.
thread_local robot_control::app::IoContext* context = nullptr;
/** Decode signed LE16 without implementation-defined conversion. */
int signed16(const can_frame& frame, unsigned offset) noexcept {
    const int raw = frame.data[offset] | (static_cast<int>(frame.data[offset + 1]) << 8);
    return raw >= 32768 ? raw - 65536 : raw;
}
/** Retain raw bytes and syscall result; a successful send is not a wire-delivery assertion. */
void record(robot_control::app::RecordKind kind, const can_frame& frame, ssize_t result, int error) noexcept {
    std::array<std::int64_t, 28> values{frame.can_id, frame.can_dlc, result, error};
    for (unsigned i = 0; i < 8; ++i)
        values[i + 4] = frame.data[i];
    values[12] = static_cast<std::int64_t>(context->monitor.fault());
    values[13] = static_cast<std::int64_t>(context->monitor.transients());
    const auto now = robot_control::app::MotionMonitor::Clock::now();
    for (unsigned i = 0; i < 2; ++i) {
        values[14 + i] = context->monitor.phase(i, now);
        values[16 + i] = context->monitor.minimum(i);
        values[18 + i] = context->monitor.maximum(i);
    }
    context->records.append(kind, values);
}
} // namespace
robot_control::app::IoBinding::IoBinding(IoContext& value) noexcept {
    context = &value;
}
robot_control::app::IoBinding::~IoBinding() {
    context = nullptr;
}

// GNU linker ABI names are required.
// NOLINTNEXTLINE(bugprone-reserved-identifier)
extern "C" ssize_t __real_send(int, const void*, size_t, int);
/** Enforce the shared zero-only setup allowlist and exact bounded RPDO targets before kernel submission. */
// NOLINTNEXTLINE(bugprone-reserved-identifier)
extern "C" ssize_t __wrap_send(int fd, const void* data, size_t length, int flags) {
    if (!context)
        return __real_send(fd, data, length, flags); // Logger child is forked before binding.
    can_frame frame{};
    if (!data || length != CAN_MTU || flags != MSG_DONTWAIT) {
        errno = EACCES;
        return -1;
    }
    std::memcpy(&frame, data, sizeof(frame));
    const int left = signed16(frame, 2), right = signed16(frame, 4);
    bool allowed = robot_control::communication::canopen::control_setup_allowed(frame);
    if (frame.can_id == 0x201) {
        const int limit = context->max_rpm * context->speed_resolution;
        const bool zero = left == 0 && right == 0;
        const auto word = frame.data[0];
        allowed = frame.can_dlc == 6 && frame.data[1] == 0
                  && (word == 0 || word == 2 || word == 6 || word == 7 || word == 15)
                  && (zero
                      || (word == 15 && !context->closed && context->records.good()
                          && context->monitor.ok(robot_control::app::MotionMonitor::Clock::now()) && left >= -limit
                          && left <= limit && right >= -limit && right <= limit && left % context->speed_resolution == 0
                          && right % context->speed_resolution == 0));
    }
    if (!allowed) {
        context->closed = true;
        record(robot_control::app::RecordKind::can_tx, frame, -1, EACCES);
        errno = EACCES;
        return -1;
    }
    const auto result = __real_send(fd, data, length, flags);
    const int error = errno;
    if (result == CAN_MTU && frame.can_id == 0x201)
        context->monitor.command(left / context->speed_resolution, -right / context->speed_resolution,
                                 robot_control::app::MotionMonitor::Clock::now());
    record(robot_control::app::RecordKind::can_tx, frame, result, result < 0 ? error : 0);
    errno = error;
    return result;
}

// NOLINTNEXTLINE(bugprone-reserved-identifier)
extern "C" ssize_t __real_recv(int, void*, size_t, int);
/** Record actual owner peeks and inspect each TPDO speed before a subsequent nonzero write. */
// NOLINTNEXTLINE(bugprone-reserved-identifier)
extern "C" ssize_t __wrap_recv(int fd, void* data, size_t length, int flags) {
    const auto result = __real_recv(fd, data, length, flags);
    const int error = errno;
    if (context && result == CAN_MTU && length == CAN_MTU && flags == (MSG_PEEK | MSG_DONTWAIT)) {
        can_frame frame{};
        std::memcpy(&frame, data, sizeof(frame));
        if (frame.can_id == 0x181 && frame.can_dlc == 8)
            context->monitor.observe(signed16(frame, 4), -signed16(frame, 6),
                                     robot_control::app::MotionMonitor::Clock::now());
        record(robot_control::app::RecordKind::can_rx, frame, result, 0);
    }
    errno = error;
    return result;
}
