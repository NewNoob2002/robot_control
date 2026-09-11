#!/bin/sh
set -eu
owner='/tmp/robot-control-qualifications/p65-observe-899914fdccb5/robot-control-zlac-qualification --interface can0 --target-once 2:5 --duration-ms 3000'
stdbuf -oL candump -L can0 | {
while IFS= read -r frame; do
    case "$frame" in
        *" can0 701#00")
            pgrep -f -x "$owner" >/dev/null || exit 1
            printf 'bootstrap_actual_boot %s\n' "$frame"
            cansend can0 601#2B171000E8030000
            echo 'bootstrap_submitted_once'
            exit 0
            ;;
    esac
done
echo 'bootstrap_rejected: capture ended without actual boot' >&2
exit 1
}
