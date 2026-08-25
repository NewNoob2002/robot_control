#include <errno.h>
#include <sys/socket.h>

/**
 * Deny one upstream CANopen transmit attempt before any socket syscall.
 *
 * @param socket_descriptor Ignored upstream socket descriptor.
 * @param buffer Ignored upstream frame buffer.
 * @param length Ignored upstream frame length.
 * @param flags Ignored upstream send flags.
 * @return Always -1 with errno set to EACCES.
 *
 * Thread safety: Reentrant; errno is thread-local. No ownership is retained.
 */
ssize_t robot_control_canopen_deny_transmit(int socket_descriptor,
                                            const void *buffer, size_t length,
                                            int flags) {
  (void)socket_descriptor;
  (void)buffer;
  (void)length;
  (void)flags;
  errno = EACCES;
  return -1;
}
