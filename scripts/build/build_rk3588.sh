#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly repo_root

sysroot="${ROBOT_CONTROL_SYSROOT:-}"
preset="${ROBOT_CONTROL_PRESET:-rk3588-release}"

case "${preset}" in
  rk3588-debug | rk3588-release) ;;
  *)
    echo "Unsupported cross-build preset: ${preset}" >&2
    exit 3
    ;;
esac

if [[ -z "${sysroot}" ]]; then
  echo "ROBOT_CONTROL_SYSROOT must name a validated Ubuntu 22.04 sysroot" >&2
  exit 2
fi
sysroot="$(realpath "${sysroot}")"
if [[ "${preset}" == "rk3588-release" ]]; then
  [[ -n "${ROBOT_CONTROL_SYSROOT_LOCK:-}" ]] || {
    echo "rk3588-release requires an explicit reviewed ROBOT_CONTROL_SYSROOT_LOCK" >&2
    exit 2
  }
  requested_sysroot_lock="${ROBOT_CONTROL_SYSROOT_LOCK}"
  [[ -f "${requested_sysroot_lock}" && ! -L "${requested_sysroot_lock}" ]] || {
    echo "Reviewed release sysroot lock must be a regular non-symlink file" >&2
    exit 3
  }
  sysroot_lock="$(realpath "${requested_sysroot_lock}")"
  reviewed_lock_root="$(realpath "${repo_root}/sysroots/locks")"
  case "${sysroot_lock}" in
    "${reviewed_lock_root}/"*.json) ;;
    *)
      echo "rk3588-release requires a lock below ${reviewed_lock_root}" >&2
      exit 3
      ;;
  esac
  reviewed_lock_path="${sysroot_lock#"${repo_root}/"}"
  git -C "${repo_root}" ls-files --error-unmatch -- \
    "${reviewed_lock_path}" >/dev/null 2>&1 || {
    echo "Release sysroot lock is not tracked by Git: ${reviewed_lock_path}" >&2
    exit 3
  }
  if ! git -C "${repo_root}" diff --quiet -- "${reviewed_lock_path}" ||
    ! git -C "${repo_root}" diff --cached --quiet -- "${reviewed_lock_path}"; then
    echo "Release sysroot lock has unreviewed working-tree changes: ${reviewed_lock_path}" >&2
    exit 3
  fi
else
  requested_sysroot_lock="${ROBOT_CONTROL_SYSROOT_LOCK:-${sysroot}.lock.json}"
  [[ -f "${requested_sysroot_lock}" && ! -L "${requested_sysroot_lock}" ]] || {
    echo "Debug sysroot lock must be a regular non-symlink file" >&2
    exit 3
  }
  sysroot_lock="$(realpath "${requested_sysroot_lock}")"
fi
readonly sysroot_lock

"${repo_root}/scripts/sysroot/validate_sysroot.sh" \
  "${sysroot}" "${sysroot_lock}"

mkdir -p \
  "${repo_root}/out/build/cross" \
  "${repo_root}/out/cache/ccache" \
  "${repo_root}/out/build-inputs" \
  "${repo_root}/out/locks"

build_input="$(mktemp -d "${repo_root}/out/build-inputs/${preset}.XXXXXX")"
readonly build_input

# Remove only the private build-input snapshot created above.
cleanup() {
  python3 - "${build_input}" <<'PY'
import shutil
import sys

shutil.rmtree(sys.argv[1], ignore_errors=True)
PY
}
trap cleanup EXIT

readonly source_snapshot="${build_input}/source"
readonly source_attestation="${build_input}/source-attestation.json"
"${repo_root}/scripts/build/create_source_snapshot.sh" \
  "${source_snapshot}" "${source_attestation}" >/dev/null

if [[ "${preset}" == "rk3588-release" ]] &&
  python3 - "${source_attestation}" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as source:
    raise SystemExit(0 if json.load(source)["dirty"] else 1)
PY
then
  echo "rk3588-release requires a clean source snapshot" >&2
  exit 3
fi

readonly snapshot_lock_file="${source_snapshot}/docker/cross/image.lock"
image_name=""
image_tag=""
image_id=""
# shellcheck disable=SC1090
source "${snapshot_lock_file}"
[[ -n "${image_name}" && -n "${image_tag}" && -n "${image_id}" ]] || {
  echo "Invalid cross-image lock in source snapshot: image identity is missing" >&2
  exit 3
}
"${source_snapshot}/scripts/build/verify_cross_image.sh" \
  "${image_id}" "${source_snapshot}" >/dev/null

# Serialize reset, configure, build, audit, and publication for this preset.
exec {build_lock_fd}>"${repo_root}/out/locks/${preset}.lock"
flock --exclusive "${build_lock_fd}"

# Deterministic snapshots normalize mtimes, so a prior incremental Ninja tree
# cannot safely prove that its objects correspond to the current snapshot.
ROBOT_CONTROL_WORKSPACE_ROOT="${repo_root}" \
  "${source_snapshot}/scripts/build/reset_cross_build_dir.sh" \
  "${preset}" >/dev/null

sysroot_content_before="$(
  awk 'NR == 1 {print $1}' \
    "${sysroot}/.robot-control/sysroot-content.sha256"
)"
target_metadata_before="$(
  sha256sum "${sysroot}/.robot-control/manifest.sha256" | awk '{print $1}'
)"
sysroot_lock_before="$(sha256sum "${sysroot_lock}" | awk '{print $1}')"
readonly sysroot_content_before target_metadata_before sysroot_lock_before

docker run --rm \
  --user "$(id -u):$(id -g)" \
  --read-only \
  --tmpfs /tmp:rw,nosuid,nodev,noexec \
  --network none \
  --cap-drop ALL \
  --security-opt no-new-privileges \
  --env HOME=/tmp/robot-control-home \
  --env ROBOT_CONTROL_SYSROOT=/opt/robot-control/sysroot \
  --mount "type=bind,source=${source_snapshot},target=/workspace,readonly" \
  --mount "type=bind,source=${repo_root}/out,target=/workspace/out" \
  --mount "type=bind,source=${repo_root}/out/cache/ccache,target=/home/builder/.cache/ccache" \
  --mount "type=bind,source=${sysroot},target=/opt/robot-control/sysroot,readonly" \
  --workdir /workspace \
  "${image_id}" \
  bash -c '
    set -euo pipefail
    mkdir -p "${HOME}"
    cmake --preset "$1"
    cmake --build --preset "$1"
  ' bash "${preset}"

"${source_snapshot}/scripts/sysroot/validate_sysroot.sh" \
  "${sysroot}" "${sysroot_lock}" >/dev/null
sysroot_content_after="$(
  awk 'NR == 1 {print $1}' \
    "${sysroot}/.robot-control/sysroot-content.sha256"
)"
target_metadata_after="$(
  sha256sum "${sysroot}/.robot-control/manifest.sha256" | awk '{print $1}'
)"
sysroot_lock_after="$(sha256sum "${sysroot_lock}" | awk '{print $1}')"
if [[ "${sysroot_content_before}" != "${sysroot_content_after}" ||
      "${target_metadata_before}" != "${target_metadata_after}" ||
      "${sysroot_lock_before}" != "${sysroot_lock_after}" ]]; then
  echo "Sysroot identity changed during the cross build; artifacts are not publishable" >&2
  exit 6
fi

readonly binary="${repo_root}/out/build/cross/${preset}/tools/platform_probe/robot-control-platform-probe"
readonly container_binary="/workspace/out/build/cross/${preset}/tools/platform_probe/robot-control-platform-probe"
docker run --rm \
  --user "$(id -u):$(id -g)" \
  --read-only \
  --tmpfs /tmp:rw,nosuid,nodev,noexec \
  --network none \
  --cap-drop ALL \
  --security-opt no-new-privileges \
  --mount "type=bind,source=${source_snapshot},target=/workspace,readonly" \
  --mount "type=bind,source=${repo_root}/out,target=/workspace/out,readonly" \
  --mount "type=bind,source=${sysroot},target=/opt/robot-control/sysroot,readonly" \
  --workdir /workspace \
  "${image_id}" \
  ./scripts/build/audit_elf.sh \
  "${container_binary}" \
  /opt/robot-control/sysroot
"${source_snapshot}/scripts/build/verify_cross_image.sh" \
  "${image_id}" "${source_snapshot}" >/dev/null
ROBOT_CONTROL_WORKSPACE_ROOT="${repo_root}" \
  "${source_snapshot}/scripts/build/write_build_metadata.sh" \
  "${preset}" \
  "${binary}" \
  "${sysroot}" \
  "${source_attestation}" \
  "${sysroot_content_before}" \
  "${target_metadata_before}" \
  "${sysroot_lock}" \
  "${sysroot_lock_before}" \
  "${source_snapshot}"
