#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "Usage: $0 <preset> <binary> <sysroot>" >&2
  exit 2
fi

readonly preset="$1"
readonly binary="$(realpath "$2")"
readonly sysroot="$(realpath "$3")"
readonly repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly output_dir="${repo_root}/out/artifacts/${preset}"
readonly container="${ROBOT_CONTROL_CONTAINER:-rk3588-dev}"
mkdir -p "${output_dir}"

binary_sha256="$(sha256sum "${binary}" | awk '{print $1}')"
source_revision="$(git -C "${repo_root}" rev-parse HEAD)"
source_dirty=false
if [[ -n "$(git -C "${repo_root}" status --porcelain=v1)" ]]; then
  source_dirty=true
fi
compiler_version="$(docker exec "${container}" \
  aarch64-linux-gnu-g++ --version | head -n 1)"
sysroot_manifest_sha256="unavailable"
if [[ -f "${sysroot}/.robot-control/manifest.sha256" ]]; then
  sysroot_manifest_sha256="$(sha256sum "${sysroot}/.robot-control/manifest.sha256" | awk '{print $1}')"
fi

cat >"${output_dir}/build-metadata.json" <<EOF
{
  "schema_version": 1,
  "source_revision": "${source_revision}",
  "source_dirty": ${source_dirty},
  "preset": "${preset}",
  "compiler": "${compiler_version}",
  "sysroot": "${sysroot}",
  "sysroot_manifest_sha256": "${sysroot_manifest_sha256}",
  "binary": "${binary}",
  "binary_sha256": "${binary_sha256}"
}
EOF

cp "${binary}" "${output_dir}/"
sha256sum "${output_dir}/$(basename "${binary}")" \
  "${output_dir}/build-metadata.json" >"${output_dir}/SHA256SUMS"
