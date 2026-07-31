#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Return the process-wide count of EasyLogger port failures. */
uint64_t robot_control_elog_failure_count(void);
/** Return the most recently observed errno or pthread error value. */
int robot_control_elog_last_error(void);
/** Reset failure diagnostics; intended for startup and deterministic tests. */
void robot_control_elog_reset_health(void);

#ifdef __cplusplus
}
#endif
