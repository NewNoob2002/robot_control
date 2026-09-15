/** @file Trace rejected sends in the existing isolated-vcan fixture without changing the gate. */
#include <errno.h>
#include <linux/can.h>
#include <stdio.h>
#include <sys/types.h>

ssize_t __real_robot_control_canopen_qualification_transmit(int socket_descriptor, const void* buffer,
                                                           size_t length, int flags);

/** Record an EACCES frame after the real gate rejects it, preserving its result and errno. */
ssize_t __wrap_robot_control_canopen_qualification_transmit(const int socket_descriptor, const void* buffer,
                                                           const size_t length, const int flags) {
    const ssize_t result = __real_robot_control_canopen_qualification_transmit(socket_descriptor, buffer, length, flags);
    const int saved_errno = errno;
    if (result < 0 && saved_errno == EACCES && buffer != NULL && length == sizeof(struct can_frame)) {
        const struct can_frame* frame = buffer;
        (void)fprintf(stderr, "REJECTED_GATE can_id=%03X dlc=%u data=", (unsigned)frame->can_id,
                      (unsigned)frame->can_dlc);
        for (unsigned i = 0; i < frame->can_dlc && i < 8U; ++i) {
            (void)fprintf(stderr, "%02X", (unsigned)frame->data[i]);
        }
        (void)fputc(10, stderr);
    }
    errno = saved_errno;
    return result;
}
