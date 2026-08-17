#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly repo_root
readonly lock_file="${repo_root}/docker/cross/image.lock"

image_name=""
image_tag=""
image_id=""
container_name=""
# shellcheck disable=SC1090
source "${lock_file}"
[[ -n "${image_name}" && -n "${image_tag}" && -n "${image_id}" ]] || {
  echo "Invalid cross-image lock: required image identity is missing" >&2
  exit 3
}

readonly image="${image_name}:${image_tag}"
"${repo_root}/scripts/build/verify_cross_image.sh" "${image}" >/dev/null

mkdir -p "${repo_root}/out/cache/ccache"

clone_id="$(
  printf '%s' "${repo_root}" | sha256sum | cut -c1-12
)"
repository_sha256="$(
  printf '%s' "${repo_root}" | sha256sum | cut -d' ' -f1
)"
readonly clone_id repository_sha256
readonly dev_container="${ROBOT_CONTROL_CONTAINER:-${container_name:-rk3588-dev}-${clone_id}}"
owner_uid="$(id -u)"
owner_gid="$(id -g)"
readonly owner_uid owner_gid

mount_args=(
  --mount "type=bind,source=${repo_root},target=/workspace,readonly"
  --mount "type=bind,source=${repo_root}/out,target=/workspace/out"
  --mount "type=bind,source=${repo_root}/out/cache/ccache,target=/home/builder/.cache/ccache"
)

sysroot_contract="none"
if [[ -n "${ROBOT_CONTROL_SYSROOT:-}" ]]; then
  sysroot="$(realpath "${ROBOT_CONTROL_SYSROOT}")"
  requested_sysroot_lock="${ROBOT_CONTROL_SYSROOT_LOCK:-${sysroot}.lock.json}"
  [[ -f "${requested_sysroot_lock}" && ! -L "${requested_sysroot_lock}" ]] || {
    echo "Container sysroot lock must be a regular non-symlink file" >&2
    exit 3
  }
  sysroot_lock="$(realpath "${requested_sysroot_lock}")"
  "${repo_root}/scripts/sysroot/validate_sysroot.sh" \
    "${sysroot}" "${sysroot_lock}"
  sysroot_content_sha256="$(
    awk 'NR == 1 {print $1}' \
      "${sysroot}/.robot-control/sysroot-content.sha256"
  )"
  sysroot_lock_sha256="$(sha256sum "${sysroot_lock}" | awk '{print $1}')"
  sysroot_contract="${sysroot}:${sysroot_content_sha256}:${sysroot_lock}:${sysroot_lock_sha256}:read-only"
  mount_args+=(
    --mount "type=bind,source=${sysroot},target=/opt/robot-control/sysroot,readonly"
  )
fi
readonly sysroot_contract

contract="$(
  printf '%s\n%s\n%s:%s\n%s\n' \
    "${repo_root}" \
    "${image_id}" \
    "${owner_uid}" \
    "${owner_gid}" \
    "${sysroot_contract}" |
    sha256sum | cut -d' ' -f1
)"
readonly contract

if docker container inspect "${dev_container}" >/dev/null 2>&1; then
  existing_managed="$(
    docker container inspect "${dev_container}" \
      --format '{{index .Config.Labels "robot-control.managed"}}'
  )"
  existing_repository="$(
    docker container inspect "${dev_container}" \
      --format '{{index .Config.Labels "robot-control.repository-sha256"}}'
  )"
  existing_clone="$(
    docker container inspect "${dev_container}" \
      --format '{{index .Config.Labels "robot-control.clone-id"}}'
  )"
  if [[ "${existing_managed}" != "true" ||
        "${existing_repository}" != "${repository_sha256}" ||
        "${existing_clone}" != "${clone_id}" ]]; then
    echo "Refusing to replace unmanaged container: ${dev_container}" >&2
    exit 4
  fi
  existing_contract="$(
    docker container inspect "${dev_container}" \
      --format '{{index .Config.Labels "robot-control.container-contract"}}'
  )"
  existing_owner_uid="$(
    docker container inspect "${dev_container}" \
      --format '{{index .Config.Labels "robot-control.owner-uid"}}'
  )"
  existing_owner_gid="$(
    docker container inspect "${dev_container}" \
      --format '{{index .Config.Labels "robot-control.owner-gid"}}'
  )"
  if [[ -z "${existing_owner_uid}" || -z "${existing_owner_gid}" ]]; then
    if [[ "${existing_contract}" != "${contract}" ]]; then
      echo "Refusing to replace container with unknown owner: ${dev_container}" >&2
      exit 4
    fi
    docker rm --force "${dev_container}" >/dev/null
  elif [[ "${existing_owner_uid}" != "${owner_uid}" ||
          "${existing_owner_gid}" != "${owner_gid}" ]]; then
    echo "Refusing to replace container owned by another user: ${dev_container}" >&2
    exit 4
  elif [[ "${existing_contract}" != "${contract}" ]]; then
    docker rm --force "${dev_container}" >/dev/null
  fi
fi

if ! docker container inspect "${dev_container}" >/dev/null 2>&1; then
  docker create \
    --name "${dev_container}" \
    --label "robot-control.managed=true" \
    --label "robot-control.repository-sha256=${repository_sha256}" \
    --label "robot-control.clone-id=${clone_id}" \
    --label "robot-control.owner-uid=${owner_uid}" \
    --label "robot-control.owner-gid=${owner_gid}" \
    --label "robot-control.container-contract=${contract}" \
    --user "${owner_uid}:${owner_gid}" \
    --read-only \
    --tmpfs /tmp:rw,nosuid,nodev,noexec \
    --network none \
    --cap-drop ALL \
    --security-opt no-new-privileges \
    --env HOME=/tmp/robot-control-home \
    --workdir /workspace \
    "${mount_args[@]}" \
    "${image_id}" \
    sleep infinity >/dev/null
fi

docker start "${dev_container}" >/dev/null
docker exec "${dev_container}" bash -lc \
  'aarch64-linux-gnu-g++ --version | head -n 1; cmake --version | head -n 1'
printf 'container=%s\nsource_mount=read-only\noutput_mount=%s/out\n' \
  "${dev_container}" "${repo_root}"
