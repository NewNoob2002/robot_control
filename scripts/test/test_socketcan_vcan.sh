#!/usr/bin/env bash
set -uo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <socketcan-test-program>" >&2
  exit 2
fi

readonly test_program=$1
if [[ ! -x "${test_program}" ]]; then
  echo "SocketCAN test program is not executable: ${test_program}" >&2
  exit 2
fi

skip() {
  echo "SKIP: $*"
  exit 77
}

[[ $(uname -s) == Linux ]] || skip "managed vcan requires Linux"
command -v unshare >/dev/null 2>&1 || skip "unshare is unavailable"
command -v ip >/dev/null 2>&1 || skip "ip is unavailable"

if ! unshare --user --map-root-user --net true >/dev/null 2>&1; then
  skip "user/network namespaces are unavailable or not permitted"
fi

# Positional parameters expand in the inner shell.
# shellcheck disable=SC2016
unshare --user --map-root-user --net -- bash -c '
  set -uo pipefail
  if ! ip link add dev vcan0 type vcan; then
    echo "SKIP: namespace capabilities or kernel vcan support are unavailable"
    exit 77
  fi
  if ! ip link set dev vcan0 up; then
    echo "SKIP: namespace-local vcan0 could not be brought up"
    exit 77
  fi
  echo "INFO: managed namespace vcan0 is up"
  export ROBOT_CONTROL_TEST_VCAN_INTERFACE=vcan0
  export ROBOT_CONTROL_TEST_ALLOW_VCAN_LINK_TOGGLE=1
  exec "$1"
' bash "${test_program}"
