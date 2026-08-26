#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Authorize exactly one node-1 Stopped or Pre-operational NMT frame.
 *
 * @param command Exact NMT command byte.
 * @return True only for 0x02 or 0x80.
 *
 * Thread safety: Authorization is isolated to the calling thread.
 */
bool robot_control_canopen_authorize_nmt(uint8_t command);

/** Identify one exact object dictionary entry without swappable arguments. */
typedef struct {
    uint16_t index;
    uint8_t subindex;
} robot_control_canopen_sdo_object_t;

/**
 * Authorize exactly one whitelisted node-1 SDO upload request.
 *
 * @param object Exact object dictionary entry.
 * @return True only for a reviewed read-only whitelist entry.
 *
 * Thread safety: Authorization is isolated to the calling thread.
 */
bool robot_control_canopen_authorize_sdo_upload(robot_control_canopen_sdo_object_t object);

/**
 * Return the exact accepted expedited upload size.
 *
 * @param object Candidate object dictionary entry.
 * @return One through four for a whitelist entry, otherwise zero.
 *
 * Thread safety: Pure and reentrant.
 */
uint8_t robot_control_canopen_sdo_upload_size(robot_control_canopen_sdo_object_t object);

/**
 * Clear any incomplete authorization owned by the current thread.
 *
 * Thread safety: Affects only the calling thread.
 */
void robot_control_canopen_clear_authorization(void);

/**
 * Validate and submit one authorized upstream CANopen frame.
 *
 * @param socket_descriptor Bound SocketCAN descriptor borrowed for the call.
 * @param buffer Upstream frame buffer borrowed for the call.
 * @param length Required to equal CAN_MTU.
 * @param flags Required to equal MSG_DONTWAIT.
 * @return send(2) result; unauthorized input returns -1 with EACCES.
 *
 * Thread safety: Uses and consumes only the calling thread's authorization.
 */
ssize_t robot_control_canopen_commissioning_transmit(int socket_descriptor, const void* buffer, size_t length,
                                                     int flags);

#ifdef __cplusplus
}
#endif
