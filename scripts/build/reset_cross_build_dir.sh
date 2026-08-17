#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <rk3588-debug|rk3588-release>" >&2
  exit 2
fi

readonly preset="$1"
case "${preset}" in
  rk3588-debug | rk3588-release) ;;
  *)
    echo "Unsupported cross-build preset: ${preset}" >&2
    exit 3
    ;;
esac

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
repo_root="$(realpath -- "${ROBOT_CONTROL_WORKSPACE_ROOT:-${repo_root}}")"
build_root="$(realpath --canonicalize-missing -- "${repo_root}/out/build/cross")"
build_dir="$(realpath --canonicalize-missing -- "${build_root}/${preset}")"
readonly repo_root build_root build_dir

case "${build_root}" in
  "${repo_root}/out/"*) ;;
  *)
    echo "Cross-build root must remain below ${repo_root}/out: ${build_root}" >&2
    exit 3
    ;;
esac

[[ "${build_dir}" == "${build_root}/${preset}" ]] || {
  echo "Unexpected cross-build directory: ${build_dir}" >&2
  exit 3
}

# A deterministic source archive assigns identical mtimes to every file.
# Reusing an older Ninja tree could therefore retain objects from different
# source content. Remove only this validated preset directory before configure,
# and fail closed if deletion is incomplete.
python3 - "${build_dir}" <<'PY'
import os
import shutil
import sys

path = sys.argv[1]
if os.path.lexists(path):
    if os.path.islink(path) or not os.path.isdir(path):
        raise SystemExit(f"Refusing to remove non-directory build path: {path}")
    shutil.rmtree(path)
if os.path.lexists(path):
    raise SystemExit(f"Cross-build directory still exists after removal: {path}")
PY
mkdir -p -- "${build_dir}"
[[ -d "${build_dir}" && ! -L "${build_dir}" ]] || {
  echo "Failed to create a regular cross-build directory: ${build_dir}" >&2
  exit 4
}
if find "${build_dir}" -mindepth 1 -print -quit | grep -q .; then
  echo "Cross-build directory is not empty after reset: ${build_dir}" >&2
  exit 4
fi

printf 'build_dir=%s\n' "${build_dir}"
