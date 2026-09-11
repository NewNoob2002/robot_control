#!/bin/sh
set +e
bin=/tmp/robot-control-qualifications/p65-observe-899914fdccb5/robot-control-zlac-qualification
"$bin" --interface can0 --target-once 2:5 --duration-ms 3000
executor_rc=$?
cansend can0 601#2B17100000000000
restore_rc=$?
printf 'wrapper_executor_rc=%d heartbeat_restore_rc=%d\n' "$executor_rc" "$restore_rc"
exit "$executor_rc"
