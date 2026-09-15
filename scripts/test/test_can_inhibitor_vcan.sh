#!/usr/bin/env bash
set -uo pipefail

[[ $# -eq 3 && -x $1 && -x $2 && -x $3 ]] || exit 2
command -v unshare >/dev/null 2>&1 || exit 77
command -v ip >/dev/null 2>&1 || exit 77
unshare --user --map-root-user --net true >/dev/null 2>&1 || exit 77

# The quoted script expands only inside the namespace's child shell.
# shellcheck disable=SC2016
unshare --user --map-root-user --net -- bash -c '
  set -euo pipefail
  ip link add dev can0 type vcan
  ip link set dev can0 up
  [[ $(ip -o link show | cut -d: -f2 | tr -d " " | sed "s/@.*//" | sort | tr "\n" " ") == "can0 lo " ]]
  "$1" "$2"
  set +e
  "$3" --interface can0 --external-loss-once --interface-inhibitor "$2" \
    >qualification.out 2>qualification.err
  result=$?
  set -e
  [[ $result -eq 1 ]]
  grep -q "interface_inhibitor_armed interface=can0" qualification.out
  grep -q "interface_inhibitor_down interface=can0" qualification.err
  ! ip -o link show dev can0 | grep -q UP
' bash "$1" "$2" "$3"
