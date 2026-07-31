#include <elog.h>

#include <errno.h>
#include <pthread.h>
#include <unistd.h>

static pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;

ElogErrCode elog_port_init(void) { return ELOG_NO_ERR; }

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
    break;
  }
}

void elog_port_output_lock(void) { (void)pthread_mutex_lock(&output_mutex); }

void elog_port_output_unlock(void) {
  (void)pthread_mutex_unlock(&output_mutex);
}

const char *elog_port_get_time(void) { return ""; }
const char *elog_port_get_p_info(void) { return ""; }
const char *elog_port_get_t_info(void) { return ""; }
