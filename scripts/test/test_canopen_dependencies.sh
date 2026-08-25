#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
verifier="${repo_root}/scripts/build/verify_canopen_dependencies.sh"
marker="${repo_root}/components/CANopenLinux/CANopenNode/.robot-control-dirty-test-$$"
readonly repo_root verifier marker

cleanup() {
  rm -f -- "${marker}"
}
trap cleanup EXIT

"${verifier}"

[[ ! -e "${repo_root}/components/CANopenNode" ]] || {
  echo "Legacy CANopenNode snapshot still exists" >&2
  exit 1
}

set +e
ROBOT_CONTROL_TEST_EXPECTED_CANOPEN_LINUX=0000000000000000000000000000000000000000 \
  "${verifier}" >/dev/null 2>&1
status=$?
set -e
[[ ${status} -eq 3 ]] || {
  echo "Wrong CANopenLinux revision returned ${status}, expected 3" >&2
  exit 1
}

[[ ! -e "${marker}" ]] || {
  echo "Dirty-tree test marker already exists: ${marker}" >&2
  exit 1
}
: >"${marker}"
set +e
"${verifier}" >/dev/null 2>&1
status=$?
set -e
[[ ${status} -eq 4 ]] || {
  echo "Dirty CANopenNode tree returned ${status}, expected 4" >&2
  exit 1
}
rm -f -- "${marker}"

echo "CANopen dependency regression checks passed"
