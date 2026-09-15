#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 2 && -x $1 && -x $2 ]] || exit 2
qualification=$1
inhibitor=$2

set +e
"$qualification" --interface can0 --external-loss-once >/dev/null 2>&1
qualification_args_rc=$?
"$inhibitor" >/dev/null 2>&1
inhibitor_args_rc=$?
set -e
[[ $qualification_args_rc -eq 2 && $inhibitor_args_rc -eq 2 ]]

# The quoted scripts expand only inside each namespace child shell.
# shellcheck disable=SC2016
unshare --user --map-root-user --net -- bash -c '
  set -euo pipefail
  ip link add dev can0 type vcan
  ip link set dev can0 up
  "$1" --interface can0 --external-loss-once --interface-inhibitor "$2" >qualification.out 2>qualification.err &
  child=$!
  for _ in $(seq 1 100); do
    grep -q "interface_inhibitor_armed interface=can0" qualification.out 2>/dev/null && break
    kill -0 "$child"
    sleep 0.01
  done
  grep -q "interface_inhibitor_armed interface=can0" qualification.out
  cansend can0 20000004#0000000000000000
  set +e
  wait "$child"
  result=$?
  set -e
  [[ $result -eq 1 ]]
  grep -q "interface_inhibitor_down interface=can0" qualification.err
  ! ip -o link show dev can0 | grep -q UP
' bash "$qualification" "$inhibitor"

# shellcheck disable=SC2016
unshare --user --map-root-user --net -- bash -c '
  set -euo pipefail
  ip link add dev can0 type vcan
  ip link set dev can0 up
  set +e
  "$1" --interface can0 --external-loss-once --interface-inhibitor "$2" >qualification.out 2>qualification.err
  result=$?
  set -e
  [[ $result -eq 1 ]]
  grep -q "interface_inhibitor_armed interface=can0" qualification.out
  grep -q "interface_inhibitor_down interface=can0" qualification.err
  ! ip -o link show dev can0 | grep -q UP
' bash "$qualification" "$inhibitor"

echo "PASS target isolated vcan: argument gates, CAN-error inhibition, failure inhibition"
