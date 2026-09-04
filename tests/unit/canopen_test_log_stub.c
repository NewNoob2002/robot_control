#include <stdarg.h>

/** Satisfy the pinned CANopenLinux logging contract without production code. */
void log_printf(int priority, const char *format, ...) {
  (void)priority;
  (void)format;
}
