#!/usr/bin/env bash
set -euo pipefail

[[ $EUID -eq 0 ]] || { echo "Run with sudo on RK3588" >&2; exit 2; }
[[ $(cat /etc/machine-id) == 6923ab3301fb4a8d816759b04ec6bf0a ]]
source_dir=/tmp/robot-control-qualifications/userspace-inhibit-fc77b14ec242
install_dir=/opt/robot-control/staging/userspace-inhibit-fc77b14ec242
qualification=$source_dir/robot-control-zlac-qualification
helper=$source_dir/robot-control-can-interface-inhibitor
echo "fc77b14e78c60ad00f3fcda704889d0c913be60ef416e77c7ba5cd9a688b6fa4  $qualification" | sha256sum -c -
echo "c2425001cbe583e3bb1e3307d4145e95cafd722d150154ff727fe519a80b1a5b  $helper" | sha256sum -c -
install -d -o root -g cat -m 0750 "$install_dir"
install -o root -g cat -m 0750 "$qualification" "$install_dir/robot-control-zlac-qualification"
install -o root -g cat -m 0750 "$helper" "$install_dir/robot-control-can-interface-inhibitor"
setcap cap_net_admin=ep "$install_dir/robot-control-can-interface-inhibitor"
[[ $(getcap "$install_dir/robot-control-can-interface-inhibitor") == "$install_dir/robot-control-can-interface-inhibitor cap_net_admin=ep" ]]
[[ -z $(getcap "$install_dir/robot-control-zlac-qualification") ]]
stat -c '%U %G %a %n' "$install_dir/robot-control-zlac-qualification" "$install_dir/robot-control-can-interface-inhibitor"
getcap "$install_dir/robot-control-can-interface-inhibitor"
echo "DONE: staged root-owned helper with cap_net_admin=ep; no CAN operation started."
