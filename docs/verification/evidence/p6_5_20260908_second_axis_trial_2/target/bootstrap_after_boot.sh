#!/bin/sh
set -eu
owner='/tmp/robot-control-qualifications/p65-observe-899914fdccb5/robot-control-zlac-qualification --interface can0 --target-once 2:5 --duration-ms 3000'
frame=$(timeout 240 candump -L -n 1 can0,701:7FF)
case "$frame" in
    *" can0 701#00") ;;
    *) echo "bootstrap_rejected: unexpected frame $frame" >&2; exit 1 ;;
esac
pgrep -f -x "$owner" >/dev/null || { echo 'bootstrap_rejected: qualification owner absent' >&2; exit 1; }
printf 'bootstrap_actual_boot %s\n' "$frame"
cansend can0 601#2B171000E8030000
echo 'bootstrap_submitted_once'
