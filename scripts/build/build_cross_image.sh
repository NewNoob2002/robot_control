#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly repo_root
readonly dockerfile="${repo_root}/docker/cross/Dockerfile"
readonly packages_lock="${repo_root}/docker/cross/packages.lock"
readonly lock_file="${repo_root}/docker/cross/image.lock"

update_lock=false
if [[ "${1:-}" == "--update-lock" ]]; then
  update_lock=true
  shift
fi
if [[ $# -ne 0 ]]; then
  echo "Usage: $0 [--update-lock]" >&2
  exit 2
fi
invocation_args=()
if [[ "${update_lock}" == true ]]; then
  invocation_args+=(--update-lock)
fi
readonly -a invocation_args

if [[ -L "${lock_file}" || ( -e "${lock_file}" && ! -f "${lock_file}" ) ]]; then
  echo "Cross-image lock must be a regular, non-symlink file: ${lock_file}" >&2
  exit 3
fi
if [[ -f "${lock_file}" ]]; then
  # shellcheck disable=SC1090
  source "${lock_file}"
fi

readonly image_name="${image_name:-rk3588-cross}"
readonly image_tag="${image_tag:-latest}"
readonly container_name="${container_name:-rk3588-dev}"
readonly base_image="${base_image:-ubuntu:22.04@sha256:3b06811b2afd352be909dd088a004166d665dc76d38b13eada33522a9d915c6f}"
readonly ubuntu_snapshot="${ubuntu_snapshot:-20260814T000000Z}"
readonly image="${image_name}:${image_tag}"

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker CLI is required to build the locked cross image" >&2
  exit 127
fi

docker_daemon_id="$(docker info --format '{{.ID}}')"
[[ -n "${docker_daemon_id}" ]] || {
  echo "Docker daemon ID is unavailable" >&2
  exit 3
}
publication_key="$(
  printf '%s\n%s\n' "${docker_daemon_id}" "${image}" |
    sha256sum | awk '{print $1}'
)"
readonly docker_daemon_id publication_key
readonly publication_lock="/tmp/robot-control-cross-image-${publication_key}.lock"
publication_lock_held=false
if [[ "${ROBOT_CONTROL_CROSS_IMAGE_LOCK_KEY:-}" == "${publication_key}" &&
      "${ROBOT_CONTROL_CROSS_IMAGE_LOCK_FD:-}" =~ ^[0-9]+$ ]]; then
  if python3 - \
    "${ROBOT_CONTROL_CROSS_IMAGE_LOCK_FD}" \
    "${publication_lock}" <<'PY'
import fcntl
import os
import stat
import sys

lock_fd = int(sys.argv[1])
lock_path = sys.argv[2]
fd_stat = os.fstat(lock_fd)
path_stat = os.stat(lock_path, follow_symlinks=False)
if (
    not stat.S_ISREG(fd_stat.st_mode)
    or fd_stat.st_nlink != 1
    or (fd_stat.st_dev, fd_stat.st_ino) != (path_stat.st_dev, path_stat.st_ino)
):
    raise SystemExit(1)
fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
PY
  then
    publication_lock_held=true
  fi
fi
if [[ "${publication_lock_held}" != true ]]; then
  exec python3 - \
    "${publication_lock}" \
    "${publication_key}" \
    "$0" \
    "${invocation_args[@]}" <<'PY'
import fcntl
import os
import stat
import sys

lock_path, lock_key, script, *script_args = sys.argv[1:]
flags = os.O_RDWR | os.O_CREAT
if hasattr(os, "O_NOFOLLOW"):
    flags |= os.O_NOFOLLOW
lock_fd = os.open(lock_path, flags, 0o666)
lock_stat = os.fstat(lock_fd)
if not stat.S_ISREG(lock_stat.st_mode) or lock_stat.st_nlink != 1:
    raise SystemExit(f"Unsafe cross-image publication lock: {lock_path}")
try:
    os.fchmod(lock_fd, 0o666)
except PermissionError:
    pass
fcntl.flock(lock_fd, fcntl.LOCK_EX)
os.set_inheritable(lock_fd, True)
environment = os.environ.copy()
environment["ROBOT_CONTROL_CROSS_IMAGE_LOCK_KEY"] = lock_key
environment["ROBOT_CONTROL_CROSS_IMAGE_LOCK_FD"] = str(lock_fd)
os.execvpe("bash", ["bash", script, *script_args], environment)
PY
fi

candidate_suffix="$(
  printf '%s:%s:%s\n' "$$" "${RANDOM}" "$(date +%s%N)" |
    sha256sum | cut -c1-16
)"
readonly candidate_image="${image_name}:candidate-${candidate_suffix}"
readonly rollback_image="${image_name}:rollback-${candidate_suffix}"

dockerfile_sha256="$(sha256sum "${dockerfile}" | awk '{print $1}')"
packages_lock_sha256="$(sha256sum "${packages_lock}" | awk '{print $1}')"
dockerfile_frontend="$(
  sed -n '1s/^# syntax=//p' "${dockerfile}"
)"
[[ "${dockerfile_frontend}" == docker/dockerfile:*@sha256:* ]] || {
  echo "Dockerfile frontend must be pinned by digest" >&2
  exit 3
}
readonly dockerfile_sha256 packages_lock_sha256 dockerfile_frontend

previous_image_id=""
if docker image inspect "${image}" >/dev/null 2>&1; then
  previous_image_id="$(
    docker image inspect "${image}" --format '{{.Id}}'
  )"
fi
readonly previous_image_id

lock_staging=""
lock_backup=""
previous_lock_sha256=""
published_lock_sha256=""
had_previous_lock=false
published_image=false
published_lock=false
rollback_image_created=false
completed=false
sync_lock_directory() {
  python3 - "$(dirname "${lock_file}")" <<'PY'
import os
import sys

directory_fd = os.open(sys.argv[1], os.O_RDONLY | os.O_DIRECTORY)
try:
    os.fsync(directory_fd)
finally:
    os.close(directory_fd)
PY
}

cleanup() {
  local status=$?
  local backup_lock_sha256=""
  local current_image_id=""
  local current_lock_sha256=""
  local rollback_failed=false
  local preserve_lock_backup=false
  set +e
  if [[ "${completed}" != true && "${published_lock}" == true ]]; then
    if [[ -f "${lock_file}" && ! -L "${lock_file}" ]]; then
      current_lock_sha256="$(sha256sum "${lock_file}" | awk '{print $1}')"
    fi
    if [[ -n "${published_lock_sha256}" &&
          "${current_lock_sha256}" == "${published_lock_sha256}" ]]; then
      if [[ "${had_previous_lock}" == true ]]; then
        if [[ -n "${lock_backup}" && -f "${lock_backup}" &&
              ! -L "${lock_backup}" ]]; then
          backup_lock_sha256="$(
            sha256sum "${lock_backup}" | awk '{print $1}'
          )"
          if [[ "${backup_lock_sha256}" != "${previous_lock_sha256}" ]]; then
            echo "Previous cross-image lock backup changed unexpectedly; retained at ${lock_backup}" >&2
            rollback_failed=true
            preserve_lock_backup=true
          elif mv -- "${lock_backup}" "${lock_file}"; then
            lock_backup=""
          else
            echo "Failed to restore previous cross-image lock; backup retained at ${lock_backup}" >&2
            rollback_failed=true
            preserve_lock_backup=true
          fi
        else
          echo "Previous cross-image lock backup is unavailable" >&2
          rollback_failed=true
        fi
      elif ! rm -f -- "${lock_file}"; then
        echo "Failed to remove newly published cross-image lock" >&2
        rollback_failed=true
      fi
    elif [[ "${had_previous_lock}" == true &&
            -n "${previous_lock_sha256}" &&
            "${current_lock_sha256}" == "${previous_lock_sha256}" ]]; then
      :
    elif [[ "${had_previous_lock}" != true && ! -e "${lock_file}" ]]; then
      :
    else
      echo "Refusing to overwrite a cross-image lock changed outside this transaction" >&2
      rollback_failed=true
      preserve_lock_backup=true
    fi
    if ! sync_lock_directory; then
      echo "Failed to fsync the cross-image lock directory during rollback" >&2
      rollback_failed=true
    fi
  fi
  if [[ "${completed}" != true && "${published_image}" == true ]]; then
    current_image_id="$(
      docker image inspect "${image}" --format '{{.Id}}' 2>/dev/null
    )"
    if [[ "${current_image_id}" == "${image_id}" ]]; then
      if [[ -n "${previous_image_id}" ]]; then
        if ! docker image tag "${previous_image_id}" "${image}" >/dev/null; then
          echo "Failed to restore previous canonical image tag" >&2
          rollback_failed=true
        fi
      elif ! docker image rm --force "${image}" >/dev/null 2>&1; then
        echo "Failed to remove newly published canonical image tag" >&2
        rollback_failed=true
      fi
    elif [[ -n "${previous_image_id}" &&
            "${current_image_id}" == "${previous_image_id}" ]]; then
      :
    elif [[ -z "${previous_image_id}" && -z "${current_image_id}" ]]; then
      :
    else
      echo "Refusing to overwrite a canonical image tag changed outside this transaction" >&2
      rollback_failed=true
    fi
  fi
  if [[ -n "${candidate_image}" ]]; then
    docker image rm --force "${candidate_image}" >/dev/null 2>&1
  fi
  if [[ -n "${lock_staging}" ]]; then
    rm -f -- "${lock_staging}"
  fi
  if [[ -n "${lock_backup}" && "${preserve_lock_backup}" != true ]]; then
    rm -f -- "${lock_backup}"
  fi
  if [[ "${rollback_image_created}" == true ]]; then
    docker image rm --force "${rollback_image}" >/dev/null 2>&1
  fi
  if [[ "${rollback_failed}" == true && "${status}" -eq 0 ]]; then
    status=1
  fi
  exit "${status}"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

docker build \
  --platform linux/amd64 \
  --provenance=false \
  --build-arg "UBUNTU_IMAGE=${base_image}" \
  --build-arg "UBUNTU_SNAPSHOT=${ubuntu_snapshot}" \
  --build-arg "RECIPE_SHA256=${dockerfile_sha256}" \
  --build-arg "PACKAGE_LOCK_SHA256=${packages_lock_sha256}" \
  --tag "${candidate_image}" \
  --file "${dockerfile}" \
  "${repo_root}"

image_id="$(docker image inspect "${candidate_image}" --format '{{.Id}}')"
architecture="$(
  docker image inspect "${candidate_image}" --format '{{.Architecture}}'
)"
mapfile -t observed < <(
  docker run --rm \
    --network none \
    --read-only \
    --cap-drop ALL \
    --security-opt no-new-privileges \
    "${candidate_image}" \
    bash -lc '
    aarch64-linux-gnu-g++ -dumpmachine
    aarch64-linux-gnu-g++ --version | head -n 1
    cmake --version | head -n 1
    ninja --version
    ld.bfd --version | head -n 1
    dpkg-query -W -f="${binary:Package}\t${Version}\t${Architecture}\n" \
      | LC_ALL=C sort | sha256sum | awk "{print \$1}"
  '
)

if [[ "${update_lock}" == true ]]; then
  lock_staging="$(mktemp "${lock_file}.staging.XXXXXX")"
  {
    printf 'schema_version=2\n'
    printf 'image_name=%q\n' "${image_name}"
    printf 'image_tag=%q\n' "${image_tag}"
    printf 'image_id=%q\n' "${image_id}"
    printf 'container_name=%q\n' "${container_name}"
    printf 'base_image=%q\n' "${base_image}"
    printf 'ubuntu_snapshot=%q\n' "${ubuntu_snapshot}"
    printf 'dockerfile_frontend=%q\n' "${dockerfile_frontend}"
    printf 'dockerfile_sha256=%q\n' "${dockerfile_sha256}"
    printf 'packages_lock_sha256=%q\n' "${packages_lock_sha256}"
    printf 'expected_architecture=%q\n' "${architecture}"
    printf 'expected_target_triplet=%q\n' "${observed[0]}"
    printf 'expected_compiler=%q\n' "${observed[1]}"
    printf 'expected_cmake=%q\n' "${observed[2]}"
    printf 'expected_ninja=%q\n' "${observed[3]}"
    printf 'expected_linker=%q\n' "${observed[4]}"
    printf 'expected_package_manifest_sha256=%q\n' "${observed[5]}"
  } >"${lock_staging}"
  chmod 0644 "${lock_staging}"
  bash -n "${lock_staging}"
  bash - "${lock_staging}" <<'BASH'
    set -euo pipefail
    schema_version=""
    image_name=""
    image_tag=""
    image_id=""
    dockerfile_frontend=""
    dockerfile_sha256=""
    packages_lock_sha256=""
    expected_architecture=""
    expected_target_triplet=""
    expected_compiler=""
    expected_cmake=""
    expected_ninja=""
    expected_linker=""
    expected_package_manifest_sha256=""
    # shellcheck disable=SC1090
    source "$1"
    [[ "${schema_version}" == "2" ]]
    [[ -n "${image_name}" && -n "${image_tag}" && -n "${image_id}" ]]
    [[ -n "${dockerfile_frontend}" && -n "${dockerfile_sha256}" ]]
    [[ -n "${packages_lock_sha256}" ]]
    [[ -n "${expected_architecture}" && -n "${expected_target_triplet}" ]]
    [[ -n "${expected_compiler}" && -n "${expected_cmake}" ]]
    [[ -n "${expected_ninja}" && -n "${expected_linker}" ]]
    [[ -n "${expected_package_manifest_sha256}" ]]
BASH
  python3 - "${lock_staging}" "$(dirname "${lock_file}")" <<'PY'
import os
import sys

file_path, directory = sys.argv[1:]
with open(file_path, "rb") as stream:
    os.fsync(stream.fileno())
directory_fd = os.open(directory, os.O_RDONLY | os.O_DIRECTORY)
try:
    os.fsync(directory_fd)
finally:
    os.close(directory_fd)
PY
  verification_lock="${lock_staging}"
  published_lock_sha256="$(
    sha256sum "${lock_staging}" | awk '{print $1}'
  )"
else
  verification_lock="${lock_file}"
fi
readonly verification_lock

"${repo_root}/scripts/build/verify_cross_image.sh" \
  "${candidate_image}" "${repo_root}" "${verification_lock}"

if [[ "${update_lock}" == true && -f "${lock_file}" ]]; then
  previous_lock_sha256="$(
    sha256sum "${lock_file}" | awk '{print $1}'
  )"
  lock_backup="$(mktemp "${lock_file}.previous.XXXXXX")"
  rm -f -- "${lock_backup}"
  ln -- "${lock_file}" "${lock_backup}"
  had_previous_lock=true
  sync_lock_directory
fi

if [[ -n "${previous_image_id}" ]]; then
  rollback_image_created=true
  docker image tag "${previous_image_id}" "${rollback_image}"
fi
published_image=true
docker image tag "${candidate_image}" "${image}"

if [[ "${update_lock}" == true ]]; then
  published_lock=true
  mv -- "${lock_staging}" "${lock_file}"
  lock_staging=""
  sync_lock_directory
fi

"${repo_root}/scripts/build/verify_cross_image.sh" "${image}"
completed=true
if [[ "${update_lock}" == true ]]; then
  echo "Updated ${lock_file}"
fi
