#include "communication/canopen/qualification_gate.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <linux/can.h>
#include <sys/socket.h>

namespace {
/** Independently constrain the final syscall to node-1 zero-only HIL operations. */
bool allowed(const can_frame& f) noexcept {
    if (f.can_id == 0)
        return f.can_dlc == 2 && f.data[1] == 1 && (f.data[0] == 1 || f.data[0] == 0x80);
    if (f.can_id == 0x201) {
        if (f.can_dlc != 6 || f.data[1] != 0)
            return false;
        for (unsigned i = 2; i < 6; ++i)
            if (f.data[i] != 0)
                return false;
        return f.data[0] == 0 || f.data[0] == 2 || f.data[0] == 6 || f.data[0] == 7 || f.data[0] == 15;
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
        errno = EACCES;
        return -1;
    }
    return __real_send(fd, buffer, length, flags);
}
