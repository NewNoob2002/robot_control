#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Identify one exact object dictionary entry without swappable arguments. */
typedef struct {
    uint16_t index;
    uint8_t subindex;
} robot_control_canopen_qualification_object_t;

/**
 * Return the exact accepted expedited upload size.
 *
 * @param object Candidate qualification readback object.
 * @return One through four for a whitelist entry, otherwise zero.
 *
 * Thread safety: Pure and reentrant.
 */
uint8_t robot_control_canopen_qualification_upload_size(robot_control_canopen_qualification_object_t object);

/**
 * Authorize exactly one node-1 qualification NMT frame.
 *
 * @param command Exact Start, Stopped, or Pre-operational command byte.
 * @return True only for 0x01, 0x02, or 0x80.
 *
 * Thread safety: Authorization is isolated to the calling thread.
 */
bool robot_control_canopen_qualification_authorize_nmt(uint8_t command);

/**
 * Authorize exactly one whitelisted node-1 SDO upload request.
 *
 * @param object Exact qualification readback object.
 * @return True only for an exact reviewed whitelist entry.
 *
 * Thread safety: Authorization is isolated to the calling thread.
 */
bool robot_control_canopen_qualification_authorize_upload(robot_control_canopen_qualification_object_t object);

/**
 * Authorize the fixed node-1 velocity-mode download 0x6060:00 = 3.
 *
 * @return True after arming exactly one matching frame.
 *
 * Thread safety: Authorization is isolated to the calling thread.
 */
bool robot_control_canopen_qualification_authorize_velocity_mode(void);

/**
 * Authorize one volatile command-application download.
 *
 * @param value Exact asynchronous zero or synchronous one value.
 * @return True only after arming one matching 0x200F:00 U16 frame.
 *
 * Thread safety: Authorization is isolated to the calling thread.
 */
bool robot_control_canopen_qualification_authorize_command_application(uint16_t value);

/**
 * Authorize one temporary heartbeat-producer download.
 *
 * @param milliseconds Exact disabled zero or qualification interval 500.
 * @return True only after arming one matching 0x1017:00 U16 frame.
 *
 * Thread safety: Authorization is isolated to the calling thread.
 */
bool robot_control_canopen_qualification_authorize_heartbeat_producer(uint16_t milliseconds);

/** Arm one node-1 0x2000:00 U16 download, accepting only 0 or 1000 ms. Thread-local, one frame. */
bool robot_control_canopen_qualification_authorize_watchdog(uint16_t milliseconds);

/** Arm one node-1 0x1800:05 U16 download, accepting only raw 0 or 100. Thread-local, one frame. */
bool robot_control_canopen_qualification_authorize_tpdo_event_timer(uint16_t value);

/**
 * Authorize one reviewed node-1 controlword download.
 *
 * @param controlword Exact 0x0006, 0x0007, or 0x000F value.
 * @return True only for a reviewed transition controlword.
 *
 * Thread safety: Authorization is isolated to the calling thread.
 */
bool robot_control_canopen_qualification_authorize_controlword(uint16_t controlword);

/**
 * Arm one exact temporary TPDO1 mapping/configuration frame or its baseline restoration.
 * @param object Only reviewed 0x1800:01/:02 or 0x1A00:00/:01/:02.
 * @param value Exact COB-ID, type, count or status/packed-speed mapping descriptor.
 * @param size Exact expedited width, one or four bytes.
 * @return True only for the fixed fixture values. Thread-local, consumed by one frame.
 */
bool robot_control_canopen_qualification_authorize_tpdo_mapping(robot_control_canopen_qualification_object_t object,
                                                                uint32_t value, uint8_t size);

/**
 * Authorize one bounded independent node-1 target download.
 *
 * @param subindex Exact independent target subindex 1 or 2.
 * @param rpm Whole-rpm target in the Phase 6 range -10 through 10.
 * @return True only for the exact subindex and bounded value.
 *
 * Thread safety: Authorization is isolated to the calling thread.
 */
bool robot_control_canopen_qualification_authorize_target(uint8_t subindex, int32_t rpm);

/** Authorize one packed target: at most one moving axis, each within +/-10 rpm.
 * @param value Low signed 16 bits are left, high signed 16 bits are right.
 * @return Whether one exact thread-local node-1 SDO frame was authorized.
 */
bool robot_control_canopen_qualification_authorize_packed_target(uint32_t value);

/** Authorize fixed RPDO1 mapping setup/restoration on the owner thread; no retained pointers. */
bool robot_control_canopen_qualification_authorize_rpdo_mapping(
    robot_control_canopen_qualification_object_t object, uint32_t value, uint8_t size);

/** Authorize one node-1 RPDO: enabled single-axis bounded target, or Shutdown with both targets zero. */
bool robot_control_canopen_qualification_authorize_rpdo(uint16_t controlword, uint32_t value);

/**
 * Clear any incomplete qualification authorization on this thread.
 *
 * Thread safety: Affects only the calling thread.
 */
void robot_control_canopen_qualification_clear_authorization(void);

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
ssize_t robot_control_canopen_qualification_transmit(int socket_descriptor, const void* buffer, size_t length,
                                                     int flags);

#ifdef __cplusplus
}
#endif
