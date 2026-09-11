#include <stdarg.h>
#include <stdio.h>
#include <string.h>

unsigned canopen_test_send_diagnostics = 0;
unsigned canopen_test_epoll_diagnostics = 0;

/** Satisfy the pinned CANopenLinux logging contract without production code. */
__attribute__((format(printf, 2, 3))) void log_printf(int priority, const char* format, ...) {
    (void)priority;
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (strstr(message, "(CO_CANsend)") != NULL) {
        ++canopen_test_send_diagnostics;
    }
    if (strstr(message, "(CO_epoll_processLast)") != NULL) {
        ++canopen_test_epoll_diagnostics;
    }
}
