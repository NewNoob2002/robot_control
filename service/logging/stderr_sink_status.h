#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Return the process-wide stderr sink failure count. Thread-safe; owns no resources. */
uint64_t robot_control_elog_failure_count(void);
/** Return the last raw output errno. Thread-safe; observed separately from the count. */
int robot_control_elog_last_error(void);
/** Reset diagnostics during quiescent test setup only; concurrent output must be stopped. */
void robot_control_elog_reset_health(void);

#ifdef __cplusplus
}
#endif
