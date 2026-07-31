#include "service/logging/easylogger_port_status.h"

#include <elog.h>

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;
static _Atomic uint64_t failure_count;
static _Atomic int last_error;

/** Record a port failure without recursively invoking the logger. */
static void record_error(int error_number) {
  atomic_store_explicit(&last_error, error_number, memory_order_relaxed);
  (void)atomic_fetch_add_explicit(&failure_count, 1U, memory_order_relaxed);
}

/** Format a positive Linux identity into a thread-local caller buffer. */
static void format_identity(char *text, size_t capacity, const char *prefix,
                            unsigned long identity) {
  const size_t prefix_size = strlen(prefix);
  if (capacity <= prefix_size + 1U) {
    record_error(EOVERFLOW);
    return;
  }
  for (size_t index = 0U; index < prefix_size; ++index) {
    text[index] = prefix[index];
  }
  char reversed[20];
  size_t digits = 0U;
  do {
    reversed[digits++] = (char)('0' + (identity % 10U));
    identity /= 10U;
  } while (identity != 0U && digits < sizeof(reversed));
  if (prefix_size + digits >= capacity) {
    text[0] = '\0';
    record_error(EOVERFLOW);
    return;
  }
  for (size_t index = 0U; index < digits; ++index) {
    text[prefix_size + index] = reversed[digits - index - 1U];
  }
  text[prefix_size + digits] = '\0';
}

/** Initialize the Linux EasyLogger port. */
ElogErrCode elog_port_init(void) { return ELOG_NO_ERR; }

/** Write one complete EasyLogger record to standard error. */
void elog_port_output(const char *log, size_t size) {
  while (size > 0U) {
    const ssize_t count = write(STDERR_FILENO, log, size);
    if (count > 0) {
      log += count;
      size -= (size_t)count;
      continue;
    }
    if (count < 0 && errno == EINTR) {
      continue;
    }
    record_error(count == 0 ? EIO : errno);
    return;
  }
}

/** Serialize EasyLogger output. */
void elog_port_output_lock(void) {
  const int result = pthread_mutex_lock(&output_mutex);
  if (result != 0) {
    record_error(result);
  }
}

/** Release EasyLogger output serialization. */
void elog_port_output_unlock(void) {
  const int result = pthread_mutex_unlock(&output_mutex);
  if (result != 0) {
    record_error(result);
  }
}

/** Return a thread-local local-time label used by optional formats. */
const char *elog_port_get_time(void) {
  static _Thread_local char text[32];
  struct timespec now = {0};
  struct tm local = {0};
  errno = 0;
  if (clock_gettime(CLOCK_REALTIME, &now) != 0 ||
      localtime_r(&now.tv_sec, &local) == NULL) {
    record_error(errno == 0 ? EIO : errno);
    return "";
  }
  if (strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &local) == 0U) {
    record_error(EOVERFLOW);
    return "";
  }
  return text;
}

/** Return a thread-local process identity label. */
const char *elog_port_get_p_info(void) {
  static _Thread_local char text[24];
  format_identity(text, sizeof(text), "pid:", (unsigned long)getpid());
  return text;
}

/** Return a thread-local Linux thread identity label. */
const char *elog_port_get_t_info(void) {
  static _Thread_local char text[24];
  const long thread_id = syscall(SYS_gettid);
  if (thread_id < 0) {
    record_error(errno);
    return "";
  }
  format_identity(text, sizeof(text), "tid:", (unsigned long)thread_id);
  return text;
}

/** Return the process-wide EasyLogger port failure count. */
uint64_t robot_control_elog_failure_count(void) {
  return atomic_load_explicit(&failure_count, memory_order_relaxed);
}

/** Return the most recent EasyLogger port error. */
int robot_control_elog_last_error(void) {
  return atomic_load_explicit(&last_error, memory_order_relaxed);
}

/** Reset EasyLogger port health diagnostics. */
void robot_control_elog_reset_health(void) {
  atomic_store_explicit(&failure_count, 0U, memory_order_relaxed);
  atomic_store_explicit(&last_error, 0, memory_order_relaxed);
}
