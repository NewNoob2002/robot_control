#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly repo_root

if [[ $# -gt 3 ]]; then
  echo "Usage: $0 [image] [source-root] [lock-file]" >&2
  exit 2
fi

source_root="$(realpath "${2:-${repo_root}}")"
readonly source_root
requested_lock_file="${3:-${source_root}/docker/cross/image.lock}"
if [[ ! -f "${requested_lock_file}" || -L "${requested_lock_file}" ]]; then
  echo "Cross-image lock must be a regular non-symlink file: ${requested_lock_file}" >&2
  exit 2
fi
lock_file="$(realpath "${requested_lock_file}")"
readonly lock_file

schema_version=""
image_name=""
image_tag=""
image_id=""
dockerfile_frontend=""
dockerfile_sha256=""
packages_lock_sha256=""
ubuntu_snapshot=""
expected_architecture=""
expected_target_triplet=""
expected_compiler=""
expected_cmake=""
expected_ninja=""
expected_linker=""
expected_package_manifest_sha256=""
# shellcheck disable=SC1090
source "${lock_file}"

[[ "${schema_version}" == "2" ]] || {
  echo "Unsupported cross-image lock schema: ${schema_version:-missing}" >&2
  exit 2
}
for required_value in \
  image_name \
  image_tag \
  image_id \
  dockerfile_frontend \
  dockerfile_sha256 \
  packages_lock_sha256 \
  ubuntu_snapshot \
  expected_architecture \
  expected_target_triplet \
  expected_compiler \
  expected_cmake \
  expected_ninja \
  expected_linker \
  expected_package_manifest_sha256; do
  if [[ -z "${!required_value}" ]]; then
    echo "Invalid cross-image lock: ${required_value} is missing" >&2
    exit 2
  fi
done

readonly image="${1:-${image_name}:${image_tag}}"

fail_mismatch() {
  local field="$1"
  local expected="$2"
  local actual="$3"
  printf 'Cross image %s mismatch\nexpected=%s\nactual=%s\n' \
    "${field}" "${expected}" "${actual}" >&2
  exit 3
}

current_dockerfile_sha256="$(
  sha256sum "${source_root}/docker/cross/Dockerfile" | awk '{print $1}'
)"
current_dockerfile_frontend="$(
  sed -n '1s/^# syntax=//p' "${source_root}/docker/cross/Dockerfile"
)"
current_packages_lock_sha256="$(
  sha256sum "${source_root}/docker/cross/packages.lock" | awk '{print $1}'
)"
[[ "${current_dockerfile_frontend}" == "${dockerfile_frontend}" ]] ||
  fail_mismatch "Dockerfile frontend" "${dockerfile_frontend}" \
    "${current_dockerfile_frontend}"
[[ "${current_dockerfile_sha256}" == "${dockerfile_sha256}" ]] ||
  fail_mismatch "source Dockerfile digest" "${dockerfile_sha256}" \
    "${current_dockerfile_sha256}"
[[ "${current_packages_lock_sha256}" == "${packages_lock_sha256}" ]] ||
  fail_mismatch "source package-lock digest" "${packages_lock_sha256}" \
    "${current_packages_lock_sha256}"

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker CLI is required to verify the locked cross image" >&2
  exit 127
fi

if ! docker image inspect "${image}" >/dev/null; then
  echo "Locked cross image is unavailable: ${image}" >&2
  echo "Run ./scripts/build/build_cross_image.sh --update-lock" >&2
  exit 2
fi

actual_image_id="$(docker image inspect "${image}" --format '{{.Id}}')"
actual_architecture="$(docker image inspect "${image}" --format '{{.Architecture}}')"
actual_recipe_sha256="$(
  docker image inspect "${image}" \
    --format '{{index .Config.Labels "robot-control.recipe-sha256"}}'
)"
actual_package_lock_sha256="$(
  docker image inspect "${image}" \
    --format '{{index .Config.Labels "robot-control.package-lock-sha256"}}'
)"
actual_snapshot="$(
  docker image inspect "${image}" \
    --format '{{index .Config.Labels "robot-control.ubuntu-snapshot"}}'
)"

[[ "${actual_image_id}" == "${image_id}" ]] ||
  fail_mismatch "ID" "${image_id}" "${actual_image_id}"
[[ "${actual_architecture}" == "${expected_architecture}" ]] ||
  fail_mismatch "architecture" "${expected_architecture}" "${actual_architecture}"
[[ "${actual_recipe_sha256}" == "${dockerfile_sha256}" ]] ||
  fail_mismatch "Dockerfile digest" "${dockerfile_sha256}" \
    "${actual_recipe_sha256}"
[[ "${actual_package_lock_sha256}" == "${packages_lock_sha256}" ]] ||
  fail_mismatch "package-lock digest" "${packages_lock_sha256}" \
    "${actual_package_lock_sha256}"
[[ "${actual_snapshot}" == "${ubuntu_snapshot}" ]] ||
  fail_mismatch "Ubuntu snapshot" "${ubuntu_snapshot}" "${actual_snapshot}"

mapfile -t observed < <(
  docker run --rm \
    --network none \
    --read-only \
    --cap-drop ALL \
    --security-opt no-new-privileges \
    "${image}" \
    bash -lc '
      aarch64-linux-gnu-g++ -dumpmachine
      aarch64-linux-gnu-g++ --version | head -n 1
      cmake --version | head -n 1
      ninja --version
      ld.bfd --version | head -n 1
      dpkg-query -W -f="${binary:Package}\t${Version}\t${Architecture}\n" \
        | LC_ALL=C sort | sha256sum | awk "{print \$1}"
      sha256sum /usr/local/share/robot-control/packages.lock \
        | awk "{print \$1}"
    '
)

[[ "${observed[0]:-}" == "${expected_target_triplet}" ]] ||
  fail_mismatch "target triplet" "${expected_target_triplet}" \
    "${observed[0]:-missing}"
[[ "${observed[1]:-}" == "${expected_compiler}" ]] ||
  fail_mismatch "compiler" "${expected_compiler}" "${observed[1]:-missing}"
[[ "${observed[2]:-}" == "${expected_cmake}" ]] ||
  fail_mismatch "CMake" "${expected_cmake}" "${observed[2]:-missing}"
[[ "${observed[3]:-}" == "${expected_ninja}" ]] ||
  fail_mismatch "Ninja" "${expected_ninja}" "${observed[3]:-missing}"
[[ "${observed[4]:-}" == "${expected_linker}" ]] ||
  fail_mismatch "linker" "${expected_linker}" "${observed[4]:-missing}"
[[ "${observed[5]:-}" == "${expected_package_manifest_sha256}" ]] ||
  fail_mismatch "installed package manifest" \
    "${expected_package_manifest_sha256}" "${observed[5]:-missing}"
[[ "${observed[6]:-}" == "${packages_lock_sha256}" ]] ||
  fail_mismatch "embedded package lock" "${packages_lock_sha256}" \
    "${observed[6]:-missing}"

printf 'image=%s\nimage_id=%s\narchitecture=%s\ntarget=%s\ncompiler=%s\ncmake=%s\nninja=%s\n' \
  "${image}" "${actual_image_id}" "${actual_architecture}" \
  "${observed[0]}" "${observed[1]}" "${observed[2]}" "${observed[3]}"
