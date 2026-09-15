#!/usr/bin/env bash
set -euo pipefail

base=/tmp/robot-control-qualifications/emergency-input-zero-v2-bb4f44251672
install_dir=/opt/robot-control/staging/emergency-input-zero-v2-bb4f44251672
expected=bb4f442516722f34236cb0261850f2cd09e0cfb6d5c86129d6b75eca6251bfe7

[[ "$(id -u)" == 0 ]]
[[ "$(cat /etc/machine-id)" == 6923ab3301fb4a8d816759b04ec6bf0a ]]
[[ "$(sha256sum "${base}/robot-control-zlac-qualification" | awk '{print $1}')" == "${expected}" ]]
install -d -o root -g cat -m 0750 "${install_dir}"
install -o root -g cat -m 0750 "${base}/robot-control-zlac-qualification" "${install_dir}/robot-control-zlac-qualification"
[[ "$(sha256sum "${install_dir}/robot-control-zlac-qualification" | awk '{print $1}')" == "${expected}" ]]
[[ "$(stat -c '%U %G %a' "${install_dir}/robot-control-zlac-qualification")" == "root cat 750" ]]
echo "DONE: staged repaired root-owned qualification ELF; no CAN operation started."
