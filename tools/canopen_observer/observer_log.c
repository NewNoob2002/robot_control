/** Suppress pinned upstream diagnostics in the bounded observer tool. */
void log_printf(const int priority, const char* format, ...) {
    (void)priority;
    (void)format;
}
