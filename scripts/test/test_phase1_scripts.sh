#!/usr/bin/env bash
set -euo pipefail

readonly repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly temp="$(mktemp -d)"
trap 'rm -rf "${temp}"' EXIT

if ROBOT_CONTROL_SYSROOT="${temp}" \
  cmake --preset rk3588-debug -S "${repo_root}" >"${temp}/cmake.log" 2>&1; then
  echo "Cross configure unexpectedly accepted an invalid sysroot" >&2
  exit 1
fi
grep -Eq 'usr/include is missing|dynamic loader missing' "${temp}/cmake.log"

if "${repo_root}/scripts/sysroot/validate_sysroot.sh" "${temp}" \
  >"${temp}/validate.log" 2>&1; then
  echo "Sysroot validator unexpectedly accepted an invalid sysroot" >&2
  exit 1
fi
grep -q 'usr/include is missing' "${temp}/validate.log"

grep -q '^export LC_ALL=C$' "${repo_root}/scripts/build/audit_elf.sh"

echo "Phase 1 negative-path script tests passed"
