#include "communication/canopen/commissioning_gate.h"

#include <errno.h>
#include <linux/can.h>
#include <string.h>
#include <sys/socket.h>

typedef struct {
    bool active;
    struct can_frame expected;
} authorization_t;

static _Thread_local authorization_t authorization;

uint8_t robot_control_canopen_sdo_upload_size(const robot_control_canopen_sdo_object_t object) {
    switch (object.index) {
        case 0x1000U:
            return object.subindex == 0U ? 4U : 0U;
        case 0x1001U:
            return object.subindex == 0U ? 1U : 0U;
        case 0x1009U:
        case 0x100AU:
        case 0x2031U:
        case 0x2035U:
            return object.subindex == 0U ? 2U : 0U;
        case 0x1018U:
            return object.subindex == 1U || object.subindex == 2U ? 4U : 0U;
        case 0x2032U:
            return object.subindex == 3U ? 2U : 0U;
        case 0x603FU:
        case 0x6041U:
            return object.subindex == 0U ? 4U : 0U;
        case 0x6061U:
            return object.subindex == 0U ? 1U : 0U;
        default:
            return 0U;
    }
}

bool robot_control_canopen_authorize_nmt(const uint8_t command) {
    robot_control_canopen_clear_authorization();
    if (command != 0x02U && command != 0x80U) {
        return false;
    }
    authorization.expected.can_id = 0x000U;
    authorization.expected.can_dlc = 2U;
    authorization.expected.data[0] = command;
    authorization.expected.data[1] = 1U;
    authorization.active = true;
    return true;
}

bool robot_control_canopen_authorize_sdo_upload(const robot_control_canopen_sdo_object_t object) {
    robot_control_canopen_clear_authorization();
    if (robot_control_canopen_sdo_upload_size(object) == 0U) {
        return false;
    }
    authorization.expected.can_id = 0x601U;
    authorization.expected.can_dlc = 8U;
    authorization.expected.data[0] = 0x40U;
    authorization.expected.data[1] = (uint8_t)object.index;
    authorization.expected.data[2] = (uint8_t)(object.index >> 8U);
    authorization.expected.data[3] = object.subindex;
    authorization.active = true;
    return true;
}

void robot_control_canopen_clear_authorization(void) {
    authorization = (authorization_t){0};
}

ssize_t robot_control_canopen_commissioning_transmit(const int socket_descriptor, const void* buffer,
                                                     const size_t length, const int flags) {
    const struct can_frame* const frame = (const struct can_frame*)buffer;
    if (!authorization.active || frame == NULL || length != CAN_MTU || flags != MSG_DONTWAIT
        || frame->can_id != authorization.expected.can_id || frame->can_dlc != authorization.expected.can_dlc
        || memcmp(frame->data, authorization.expected.data, sizeof(frame->data)) != 0) {
        robot_control_canopen_clear_authorization();
        errno = EACCES;
        return -1;
    }

    const ssize_t sent = send(socket_descriptor, buffer, length, flags);
    if (sent >= 0 || (errno != EINTR && errno != EAGAIN && errno != ENOBUFS)) {
        robot_control_canopen_clear_authorization();
    }
    return sent;
}
