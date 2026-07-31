#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <sysroot>" >&2
  exit 2
fi

readonly sysroot="$(realpath "$1")"
readonly os_release="${sysroot}/etc/os-release"

[[ -d "${sysroot}/usr/include" ]] || {
  echo "Invalid sysroot: usr/include is missing" >&2
  exit 3
}

loader=""
for candidate in \
  "${sysroot}/lib/ld-linux-aarch64.so.1" \
  "${sysroot}/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1"; do
  if [[ -e "${candidate}" ]]; then
    loader="${candidate}"
    break
  fi
done
[[ -n "${loader}" ]] || {
  echo "Invalid sysroot: aarch64 dynamic loader is missing" >&2
  exit 4
}

file "${loader}" | grep -q 'ARM aarch64' || {
  echo "Invalid sysroot: loader is not aarch64" >&2
  exit 5
}

if [[ -f "${os_release}" ]]; then
  # shellcheck disable=SC1090
  source "${os_release}"
  [[ "${ID:-}" == "debian" && "${VERSION_ID:-}" == "11" ]] || {
    echo "Sysroot is not Debian 11: ID=${ID:-unknown} VERSION_ID=${VERSION_ID:-unknown}" >&2
    exit 6
  }
elif [[ -f "${sysroot}/.robot-control/os-release" ]]; then
  # shellcheck disable=SC1090
  source "${sysroot}/.robot-control/os-release"
  [[ "${ID:-}" == "debian" && "${VERSION_ID:-}" == "11" ]] || exit 6
else
  echo "Invalid sysroot: os-release metadata is missing" >&2
  exit 7
fi

printf 'sysroot=%s\nloader=%s\nos=%s %s\n' \
  "${sysroot}" "${loader}" "${PRETTY_NAME:-Debian}" "${VERSION_ID}"

