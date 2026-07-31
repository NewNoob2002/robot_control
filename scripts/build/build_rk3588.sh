#!/usr/bin/env bash
set -euo pipefail

readonly repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly lock_file="${repo_root}/docker/cross/image.lock"

# shellcheck disable=SC1090
source "${lock_file}"

sysroot="${ROBOT_CONTROL_SYSROOT:-}"
preset="${ROBOT_CONTROL_PRESET:-rk3588-release}"
container="${ROBOT_CONTROL_CONTAINER:-${container_name}}"

if [[ -z "${sysroot}" ]]; then
  echo "ROBOT_CONTROL_SYSROOT must name a validated Ubuntu 22.04 sysroot" >&2
  exit 2
fi
sysroot="$(realpath "${sysroot}")"

"${repo_root}/scripts/sysroot/validate_sysroot.sh" "${sysroot}"

mkdir -p "${repo_root}/out"

container_state="$(docker inspect --format '{{.State.Running}}' "${container}" 2>/dev/null || true)"
if [[ "${container_state}" != "true" ]]; then
  echo "Required build container is not running: ${container}" >&2
  exit 3
fi

container_image_id="$(docker inspect --format '{{.Image}}' "${container}")"
if [[ "${container_image_id}" != "${image_id}" ]]; then
  echo "Build container image mismatch" >&2
  echo "expected=${image_id}" >&2
  echo "actual=${container_image_id}" >&2
  exit 4
fi

case "${sysroot}" in
  "${repo_root}"/*)
    container_sysroot="/workspace/${sysroot#${repo_root}/}"
    ;;
  *)
    echo "When reusing ${container}, ROBOT_CONTROL_SYSROOT must be below ${repo_root}" >&2
    echo "Place it under the ignored sysroots/ directory or recreate the container with an extra read-only mount." >&2
    exit 5
    ;;
esac

docker exec \
  --user "$(id -u):$(id -g)" \
  --env HOME=/tmp/robot-control-home-"$(id -u)" \
  --env ROBOT_CONTROL_SYSROOT="${container_sysroot}" \
  --workdir /workspace \
  "${container}" \
  bash -lc "mkdir -p \"\$HOME\" && cmake --preset '${preset}' && cmake --build --preset '${preset}'"

readonly binary="${repo_root}/out/build/${preset}/tools/platform_probe/robot-control-platform-probe"
"${repo_root}/scripts/build/audit_elf.sh" "${binary}" "${sysroot}"
ROBOT_CONTROL_CONTAINER="${container}" \
  "${repo_root}/scripts/build/write_build_metadata.sh" "${preset}" "${binary}" "${sysroot}"
