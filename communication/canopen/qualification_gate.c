#include "communication/canopen/qualification_gate.h"

#include <errno.h>
#include <linux/can.h>
#include <string.h>
#include <sys/socket.h>

typedef struct {
    bool active;
    struct can_frame expected;
} qualification_authorization_t;

static _Thread_local qualification_authorization_t authorization;

typedef struct {
    uint8_t command;
    uint16_t index;
    uint8_t subindex;
    uint32_t value;
} qualification_download_t;

/** Arm one exact frame after clearing any older authorization. */
static bool authorize_frame(const struct can_frame expected) {
    robot_control_canopen_qualification_clear_authorization();
    authorization.expected = expected;
    authorization.active = true;
    return true;
}

/** Build one exact expedited SDO download frame. */
static struct can_frame download_frame(const qualification_download_t request) {
    struct can_frame frame = {0};
    frame.can_id = 0x601U;
    frame.can_dlc = 8U;
    frame.data[0] = request.command;
    frame.data[1] = (uint8_t)request.index;
    frame.data[2] = (uint8_t)(request.index >> 8U);
    frame.data[3] = request.subindex;
    frame.data[4] = (uint8_t)request.value;
    frame.data[5] = (uint8_t)(request.value >> 8U);
    frame.data[6] = (uint8_t)(request.value >> 16U);
    frame.data[7] = (uint8_t)(request.value >> 24U);
    return frame;
}

uint8_t robot_control_canopen_qualification_upload_size(const robot_control_canopen_qualification_object_t object) {
    switch (object.index) {
        case 0x1017U:
        case 0x2000U:
        case 0x200FU:
        case 0x6040U:
        case 0x605AU:
            return object.subindex == 0U ? 2U : 0U;
        case 0x1400U:
        case 0x1800U:
            return object.subindex == 1U ? 4U : object.subindex == 2U ? 1U : object.subindex == 5U ? 2U : 0U;
        case 0x1600U:
        case 0x1A00U:
            return object.subindex == 0U ? 1U : object.subindex == 1U || object.subindex == 2U ? 4U : 0U;
        case 0x603FU:
        case 0x6041U:
            return object.subindex == 0U ? 4U : 0U;
        case 0x6060U:
        case 0x6061U:
            return object.subindex == 0U ? 1U : 0U;
        case 0x606CU:
        case 0x60FFU:
            return object.subindex >= 1U && object.subindex <= 3U ? 4U : 0U;
        default:
            return 0U;
    }
}

bool robot_control_canopen_qualification_authorize_nmt(const uint8_t command) {
    if (command != 0x01U && command != 0x02U && command != 0x80U) {
        robot_control_canopen_qualification_clear_authorization();
        return false;
    }
    struct can_frame frame = {0};
    frame.can_id = 0x000U;
    frame.can_dlc = 2U;
    frame.data[0] = command;
    frame.data[1] = 1U;
    return authorize_frame(frame);
}

bool robot_control_canopen_qualification_authorize_upload(const robot_control_canopen_qualification_object_t object) {
    if (robot_control_canopen_qualification_upload_size(object) == 0U) {
        robot_control_canopen_qualification_clear_authorization();
        return false;
    }
    return authorize_frame(download_frame(
        (qualification_download_t){.command = 0x40U, .index = object.index, .subindex = object.subindex, .value = 0U}));
}

bool robot_control_canopen_qualification_authorize_velocity_mode(void) {
    return authorize_frame(
        download_frame((qualification_download_t){.command = 0x2FU, .index = 0x6060U, .subindex = 0U, .value = 3U}));
}

bool robot_control_canopen_qualification_authorize_command_application(const uint16_t value) {
    if (value > 1U) {
        robot_control_canopen_qualification_clear_authorization();
        return false;
    }
    return authorize_frame(
        download_frame((qualification_download_t){.command = 0x2BU, .index = 0x200FU, .subindex = 0U, .value = value}));
}

bool robot_control_canopen_qualification_authorize_heartbeat_producer(const uint16_t milliseconds) {
    if (milliseconds != 0U && milliseconds != 500U) {
        robot_control_canopen_qualification_clear_authorization();
        return false;
    }
    return authorize_frame(download_frame(
        (qualification_download_t){.command = 0x2BU, .index = 0x1017U, .subindex = 0U, .value = milliseconds}));
}

bool robot_control_canopen_qualification_authorize_controlword(const uint16_t controlword) {
    if (controlword != 0x0000U && controlword != 0x0002U && controlword != 0x0006U && controlword != 0x0007U
        && controlword != 0x000FU) {
        robot_control_canopen_qualification_clear_authorization();
        return false;
    }
    return authorize_frame(download_frame(
        (qualification_download_t){.command = 0x2BU, .index = 0x6040U, .subindex = 0U, .value = controlword}));
}

bool robot_control_canopen_qualification_authorize_watchdog(const uint16_t milliseconds) {
    if (milliseconds != 0U && milliseconds != 1000U) {
        robot_control_canopen_qualification_clear_authorization();
        return false;
    }
    return authorize_frame(download_frame(
        (qualification_download_t){.command = 0x2BU, .index = 0x2000U, .subindex = 0U, .value = milliseconds}));
}

bool robot_control_canopen_qualification_authorize_tpdo_event_timer(const uint16_t value) {
    if (value != 0U && value != 100U) {
        robot_control_canopen_qualification_clear_authorization();
        return false;
    }
    return authorize_frame(
        download_frame((qualification_download_t){.command = 0x2BU, .index = 0x1800U, .subindex = 5U, .value = value}));
}

bool robot_control_canopen_qualification_authorize_tpdo_mapping(
    const robot_control_canopen_qualification_object_t object, const uint32_t value, const uint8_t size) {
    const bool valid =
        (object.index == 0x1800U && object.subindex == 1U && size == 4U && (value == 0x181U || value == 0x80000181U))
        || (object.index == 0x1800U && object.subindex == 2U && size == 1U && value == 255U)
        || (object.index == 0x1A00U && object.subindex == 0U && size == 1U && (value == 0U || value == 2U))
        || (object.index == 0x1A00U && (object.subindex == 1U || object.subindex == 2U) && size == 4U
            && (value == 0x606C0320U || value == 0x60410020U));
    if (!valid) {
        robot_control_canopen_qualification_clear_authorization();
        return false;
    }
    return authorize_frame(download_frame((qualification_download_t){
        .command = size == 1U ? 0x2FU : 0x23U, .index = object.index, .subindex = object.subindex, .value = value}));
}

bool robot_control_canopen_qualification_authorize_target(const uint8_t subindex, const int32_t rpm) {
    if ((subindex != 1U && subindex != 2U) || rpm < -10 || rpm > 10) {
        robot_control_canopen_qualification_clear_authorization();
        return false;
    }
    return authorize_frame(download_frame(
        (qualification_download_t){.command = 0x23U, .index = 0x60FFU, .subindex = subindex, .value = (uint32_t)rpm}));
}

bool robot_control_canopen_qualification_authorize_packed_target(const uint32_t value) {
    const uint32_t low = value & 0xFFFFU;
    const uint32_t high = value >> 16U;
    if ((low > 10U && low < 65526U) || (high > 10U && high < 65526U) || (low != 0U && high != 0U)) {
        robot_control_canopen_qualification_clear_authorization();
        return false;
    }
    return authorize_frame(download_frame(
        (qualification_download_t){.command = 0x23U, .index = 0x60FFU, .subindex = 3U, .value = value}));
}

bool robot_control_canopen_qualification_authorize_rpdo_mapping(
    const robot_control_canopen_qualification_object_t object, const uint32_t value, const uint8_t size) {
    const bool valid =
        (object.index == 0x1400U && object.subindex == 1U && size == 4U && (value == 0x201U || value == 0x80000201U))
        || (object.index == 0x1600U && object.subindex == 0U && size == 1U && (value == 0U || value == 2U))
        || (object.index == 0x1600U && object.subindex == 1U && size == 4U && value == 0x60400010U)
        || (object.index == 0x1600U && object.subindex == 2U && size == 4U
            && (value == 0x60FF0320U || value == 0x60600008U));
    if (!valid) {
        robot_control_canopen_qualification_clear_authorization();
        return false;
    }
    return authorize_frame(download_frame((qualification_download_t){
        .command = size == 1U ? 0x2FU : 0x23U, .index = object.index, .subindex = object.subindex, .value = value}));
}

bool robot_control_canopen_qualification_authorize_rpdo(const uint16_t controlword, const uint32_t value) {
    if (!robot_control_canopen_qualification_authorize_packed_target(value)
        || !((controlword == 0x000FU && value != 0U) || (controlword == 0x0006U && value == 0U))) {
        robot_control_canopen_qualification_clear_authorization();
        return false;
    }
    struct can_frame frame = {0};
    frame.can_id = 0x201U;
    frame.can_dlc = 6U;
    frame.data[0] = (uint8_t)controlword;
    frame.data[1] = (uint8_t)(controlword >> 8U);
    frame.data[2] = (uint8_t)value;
    frame.data[3] = (uint8_t)(value >> 8U);
    frame.data[4] = (uint8_t)(value >> 16U);
    frame.data[5] = (uint8_t)(value >> 24U);
    return authorize_frame(frame);
}

void robot_control_canopen_qualification_clear_authorization(void) {
    authorization = (qualification_authorization_t){0};
}

ssize_t robot_control_canopen_qualification_transmit(const int socket_descriptor, const void* buffer,
                                                     const size_t length, const int flags) {
    const struct can_frame* const frame = (const struct can_frame*)buffer;
    if (!authorization.active || frame == NULL || length != CAN_MTU || flags != MSG_DONTWAIT
        || frame->can_id != authorization.expected.can_id || frame->can_dlc != authorization.expected.can_dlc
        || memcmp(frame->data, authorization.expected.data, sizeof(frame->data)) != 0) {
        robot_control_canopen_qualification_clear_authorization();
        errno = EACCES;
        return -1;
    }

    const ssize_t sent = send(socket_descriptor, buffer, length, flags);
    if (sent >= 0 || (errno != EINTR && errno != EAGAIN && errno != ENOBUFS)) {
        robot_control_canopen_qualification_clear_authorization();
    }
    return sent;
}
