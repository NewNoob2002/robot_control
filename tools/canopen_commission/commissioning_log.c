#include <stdarg.h>
#include <stdio.h>

/** Forward pinned CANopenLinux diagnostics to the commissioning stderr stream. */
void log_printf(const int priority, const char* format, ...) {
    (void)priority;
    va_list arguments;
    va_start(arguments, format);
    (void)vfprintf(stderr, format, arguments);
    (void)fputc('\n', stderr);
    va_end(arguments);
}
