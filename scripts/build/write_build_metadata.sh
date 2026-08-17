#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 9 ]]; then
  echo "Usage: $0 <preset> <binary> <sysroot> <source-attestation> <sysroot-content-sha256> <target-metadata-sha256> <sysroot-lock> <sysroot-lock-sha256> <source-snapshot>" >&2
  exit 2
fi

preset="$1"
case "${preset}" in
  rk3588-debug | rk3588-release) ;;
  *)
    echo "Unsupported artifact preset: ${preset}" >&2
    exit 3
    ;;
esac
binary="$(realpath "$2")"
sysroot="$(realpath "$3")"
source_attestation="$(realpath "$4")"
sysroot_content_sha256="$5"
target_metadata_sha256="$6"
sysroot_lock="$(realpath "$7")"
sysroot_lock_sha256="$8"
source_snapshot="$(realpath "$9")"
script_repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
repo_root="$(realpath -- "${ROBOT_CONTROL_WORKSPACE_ROOT:-${script_repo_root}}")"
readonly preset binary sysroot source_attestation sysroot_content_sha256
readonly target_metadata_sha256 sysroot_lock sysroot_lock_sha256 source_snapshot
readonly script_repo_root repo_root
readonly artifact_parent="${repo_root}/out/artifacts"
output_dir="$(
  realpath --canonicalize-missing -- "${artifact_parent}/${preset}"
)"
case "${output_dir}" in
  "${artifact_parent}/"*) ;;
  *)
    echo "Artifact output must remain below ${artifact_parent}: ${output_dir}" >&2
    exit 3
    ;;
esac
readonly output_dir
readonly lock_file="${source_snapshot}/docker/cross/image.lock"
image_lock_sha256="$(sha256sum "${lock_file}" | awk '{print $1}')"
readonly image_lock_sha256

image_name=""
image_tag=""
image_id=""
dockerfile_sha256=""
packages_lock_sha256=""
# shellcheck disable=SC1090
source "${lock_file}"
[[ -n "${image_name}" && -n "${image_tag}" && -n "${image_id}" &&
  -n "${dockerfile_sha256}" && -n "${packages_lock_sha256}" ]] || {
  echo "Invalid cross-image lock: required image identity is missing" >&2
  exit 3
}

readonly manifest_checksum="${sysroot}/.robot-control/sysroot-content.sha256"
[[ -f "${manifest_checksum}" ]] || {
  echo "Verified sysroot content manifest is missing: ${manifest_checksum}" >&2
  exit 3
}
[[ "$(awk 'NR == 1 {print $1}' "${manifest_checksum}")" == \
  "${sysroot_content_sha256}" ]] || {
  echo "Sysroot content identity no longer matches the build attestation" >&2
  exit 3
}
[[ "$(sha256sum "${sysroot}/.robot-control/manifest.sha256" | awk '{print $1}')" == \
  "${target_metadata_sha256}" ]] || {
  echo "Target metadata identity no longer matches the build attestation" >&2
  exit 3
}
[[ "$(sha256sum "${sysroot_lock}" | awk '{print $1}')" == \
  "${sysroot_lock_sha256}" ]] || {
  echo "External sysroot lock identity no longer matches the build attestation" >&2
  exit 3
}

binary_sha256="$(sha256sum "${binary}" | awk '{print $1}')"
dockerfile_sha256_actual="$(
  sha256sum "${source_snapshot}/docker/cross/Dockerfile" | awk '{print $1}'
)"
packages_lock_sha256_actual="$(
  sha256sum "${source_snapshot}/docker/cross/packages.lock" | awk '{print $1}'
)"
[[ "${dockerfile_sha256_actual}" == "${dockerfile_sha256}" ]] || {
  echo "Source snapshot Dockerfile does not match the verified image lock" >&2
  exit 3
}
[[ "${packages_lock_sha256_actual}" == "${packages_lock_sha256}" ]] || {
  echo "Source snapshot package lock does not match the verified image lock" >&2
  exit 3
}
[[ "$(sha256sum "${lock_file}" | awk '{print $1}')" == \
  "${image_lock_sha256}" ]] || {
  echo "Cross-image lock changed while build metadata was being prepared" >&2
  exit 3
}
toolchain_sha256="$(
  sha256sum \
    "${source_snapshot}/cmake/toolchains/aarch64-rk3588-ubuntu2204.cmake" |
    awk '{print $1}'
)"
presets_sha256="$(
  sha256sum "${source_snapshot}/CMakePresets.json" | awk '{print $1}'
)"

mapfile -t tools < <(
  docker run --rm --network none --read-only "${image_id}" bash -lc '
    aarch64-linux-gnu-g++ --version | head -n 1
    ld.bfd --version | head -n 1
    cmake --version | head -n 1
    ninja --version
  '
)

artifact_name="$(basename "${binary}")"
sysroot_artifact_id="$(basename "${sysroot}")"

mkdir -p "${artifact_parent}"
staging="$(mktemp -d "${artifact_parent}/.${preset}.staging.XXXXXX")"
staging_identity="$(stat -c '%d:%i' "${staging}")"
backup=""
publication_committed=false
readonly staging_identity

# Remove only the transaction's unpublished directory and restore the previous
# publication after a caught interruption or failed promotion.
cleanup() {
  local output_identity=""

  if [[ "${publication_committed:-false}" != true ]]; then
    if [[ -d "${output_dir}" && ! -L "${output_dir}" ]]; then
      output_identity="$(stat -c '%d:%i' "${output_dir}")"
      if [[ "${output_identity}" == "${staging_identity}" ]]; then
        python3 - "${output_dir}" <<'PY'
import shutil
import sys

shutil.rmtree(sys.argv[1], ignore_errors=True)
PY
      fi
    fi
    if [[ -n "${backup:-}" && ! -e "${output_dir}" && ! -L "${output_dir}" ]]; then
      if mv -- "${backup}" "${output_dir}"; then
        backup=""
      else
        echo "Artifact rollback failed; prior output remains at ${backup}" >&2
      fi
    fi
  fi

  if [[ -n "${staging:-}" ]]; then
    python3 - "${staging}" <<'PY'
import shutil
import sys

shutil.rmtree(sys.argv[1], ignore_errors=True)
PY
  fi
  if [[ "${publication_committed:-false}" == true && -n "${backup:-}" ]]; then
    python3 - "${backup}" <<'PY'
import shutil
import sys

shutil.rmtree(sys.argv[1], ignore_errors=True)
PY
  fi
}
trap cleanup EXIT

python3 - \
  "${staging}/build-metadata.json" \
  "${source_attestation}" \
  "${preset}" \
  "${image_name}:${image_tag}" \
  "${image_id}" \
  "${dockerfile_sha256_actual}" \
  "${packages_lock_sha256_actual}" \
  "${image_lock_sha256}" \
  "${tools[0]}" \
  "${tools[1]}" \
  "${tools[2]}" \
  "${tools[3]}" \
  "${toolchain_sha256}" \
  "${presets_sha256}" \
  "${sysroot_artifact_id}" \
  "${sysroot_content_sha256}" \
  "${target_metadata_sha256}" \
  "${sysroot_lock_sha256}" \
  "${artifact_name}" \
  "${binary_sha256}" <<'PY'
import json
import sys

(
    output,
    source_attestation,
    preset,
    image,
    image_id,
    dockerfile_sha256,
    packages_lock_sha256,
    image_lock_sha256,
    compiler,
    linker,
    cmake,
    ninja,
    toolchain_sha256,
    presets_sha256,
    sysroot_artifact_id,
    sysroot_content_sha256,
    target_metadata_sha256,
    sysroot_lock_sha256,
    binary,
    binary_sha256,
) = sys.argv[1:]

with open(source_attestation, encoding="utf-8") as stream:
    source = json.load(stream)

document = {
    "schema_version": 2,
    "source": source,
    "build": {
        "preset": preset,
        "cmake_presets_sha256": presets_sha256,
        "toolchain_sha256": toolchain_sha256,
    },
    "container": {
        "image": image,
        "image_id": image_id,
        "dockerfile_sha256": dockerfile_sha256,
        "packages_lock_sha256": packages_lock_sha256,
        "image_lock_sha256": image_lock_sha256,
    },
    "tools": {
        "compiler": compiler,
        "linker": linker,
        "cmake": cmake,
        "ninja": ninja,
    },
    "sysroot": {
        "artifact_id": sysroot_artifact_id,
        "content_sha256": sysroot_content_sha256,
        "target_metadata_sha256": target_metadata_sha256,
        "external_lock_sha256": sysroot_lock_sha256,
    },
    "artifact": {
        "path": binary,
        "sha256": binary_sha256,
    },
}

with open(output, "w", encoding="utf-8") as stream:
    json.dump(document, stream, indent=2, sort_keys=True)
    stream.write("\n")
PY

cp "${binary}" "${staging}/${artifact_name}"
mkdir -p "${staging}/.robot-control"
printf 'robot-control-artifact-v1\n' \
  >"${staging}/.robot-control/managed-artifact"
(
  cd "${staging}"
  sha256sum \
    "${artifact_name}" \
    build-metadata.json \
    .robot-control/managed-artifact \
    >SHA256SUMS
)

if [[ -e "${output_dir}" || -L "${output_dir}" ]]; then
  [[ -d "${output_dir}" && ! -L "${output_dir}" ]] || {
    echo "Refusing to replace a non-directory artifact output: ${output_dir}" >&2
    exit 4
  }
  [[ -f "${output_dir}/.robot-control/managed-artifact" &&
    "$(<"${output_dir}/.robot-control/managed-artifact")" == \
      "robot-control-artifact-v1" ]] || {
    echo "Refusing to replace an unmanaged artifact output: ${output_dir}" >&2
    exit 4
  }
  backup="$(mktemp -d "${artifact_parent}/.${preset}.previous.XXXXXX")"
  rmdir -- "${backup}"
  mv -- "${output_dir}" "${backup}"
fi

mv -- "${staging}" "${output_dir}"
staging=""
publication_committed=true
