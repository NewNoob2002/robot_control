#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <sysroot> [external-lock.json]" >&2
  exit 2
fi

sysroot="$(realpath "$1")"
readonly sysroot
external_lock=""
if [[ $# -eq 2 ]]; then
  requested_external_lock="$2"
  [[ -f "${requested_external_lock}" && ! -L "${requested_external_lock}" ]] || {
    echo "Invalid sysroot lock: external lock is not a regular non-symlink file" >&2
    exit 11
  }
  external_lock="$(realpath "${requested_external_lock}")"
fi
readonly external_lock
readonly metadata="${sysroot}/.robot-control"
readonly metadata_checksum="${metadata}/manifest.sha256"
readonly content_manifest="${metadata}/sysroot-content.jsonl"
readonly content_checksum="${metadata}/sysroot-content.sha256"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly script_dir

[[ -d "${sysroot}/usr/include" ]] || {
  echo "Invalid sysroot: usr/include is missing" >&2
  exit 3
}

[[ -d "${metadata}" ]] || {
  echo "Invalid sysroot: target metadata is missing" >&2
  exit 4
}

[[ -f "${metadata_checksum}" ]] || {
  echo "Invalid sysroot: target metadata checksum is missing" >&2
  exit 4
}

readonly expected_metadata_files=(
  architecture.txt
  glibc-version.txt
  ldd-version.txt
  os-release
  packages.tsv
  sysroot-files.txt
  uname.txt
)

for metadata_file in "${expected_metadata_files[@]}"; do
  [[ -f "${metadata}/${metadata_file}" ]] || {
    echo "Invalid sysroot: target metadata ${metadata_file} is missing" >&2
    exit 4
  }
done

if grep -Evq '^[0-9a-f]{64}  [A-Za-z0-9][A-Za-z0-9._-]*$' \
  "${metadata_checksum}"; then
  echo "Invalid sysroot: target metadata checksum contains a non-relative name" >&2
  exit 4
fi
if [[ "$(wc -l <"${metadata_checksum}")" -ne "${#expected_metadata_files[@]}" ]]; then
  echo "Invalid sysroot: target metadata checksum has unexpected entries" >&2
  exit 4
fi
for metadata_file in "${expected_metadata_files[@]}"; do
  grep -Eq "^[0-9a-f]{64}  ${metadata_file//./\\.}$" "${metadata_checksum}" || {
    echo "Invalid sysroot: target metadata checksum omits ${metadata_file}" >&2
    exit 4
  }
done

if ! (cd "${metadata}" && sha256sum --check --status manifest.sha256); then
  echo "Invalid sysroot: target metadata checksum verification failed" >&2
  exit 4
fi

[[ "$(<"${metadata}/architecture.txt")" == "arm64" ]] || {
  echo "Invalid sysroot: target architecture is not arm64" >&2
  exit 5
}

os_id="$(sed -n 's/^ID=//p' "${metadata}/os-release" | tail -n 1)"
os_version="$(sed -n 's/^VERSION_ID=//p' "${metadata}/os-release" | tail -n 1)"
os_pretty_name="$(sed -n 's/^PRETTY_NAME=//p' "${metadata}/os-release" | tail -n 1)"
os_id="${os_id#\"}"
os_id="${os_id%\"}"
os_version="${os_version#\"}"
os_version="${os_version%\"}"
os_pretty_name="${os_pretty_name#\"}"
os_pretty_name="${os_pretty_name%\"}"
[[ "${os_id}" == "ubuntu" && "${os_version}" == "22.04" ]] || {
  echo "Sysroot is not Ubuntu 22.04: ID=${os_id:-unknown} VERSION_ID=${os_version:-unknown}" >&2
  exit 6
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
  exit 7
}

file -L "${loader}" | grep -q 'ARM aarch64' || {
  echo "Invalid sysroot: loader is not aarch64" >&2
  exit 8
}

[[ -f "${content_manifest}" ]] || {
  echo "Invalid sysroot: content manifest is missing" >&2
  exit 9
}
[[ -f "${content_checksum}" ]] || {
  echo "Invalid sysroot: content manifest checksum is missing" >&2
  exit 9
}
grep -Eq '^[0-9a-f]{64}  sysroot-content\.jsonl$' "${content_checksum}" || {
  echo "Invalid sysroot: content manifest checksum name is not relative" >&2
  exit 9
}
if [[ "$(wc -l <"${content_checksum}")" -ne 1 ]]; then
  echo "Invalid sysroot: content manifest checksum has unexpected entries" >&2
  exit 9
fi
if ! (cd "${metadata}" && sha256sum --check --status sysroot-content.sha256); then
  echo "Invalid sysroot: content manifest checksum verification failed" >&2
  exit 9
fi

temporary="$(mktemp -d)"
trap 'rm -rf "${temporary}"' EXIT
if ! "${script_dir}/generate_content_manifest.py" \
  "${sysroot}" \
  --output "${temporary}/sysroot-content.jsonl" \
  --checksum-output "${temporary}/sysroot-content.sha256" \
  >/dev/null; then
  echo "Invalid sysroot: content tree contains an unsafe symlink" >&2
  exit 10
fi
if ! cmp -s "${content_manifest}" "${temporary}/sysroot-content.jsonl"; then
  echo "Invalid sysroot: current tree does not match content manifest" >&2
  exit 10
fi

if [[ -n "${external_lock}" ]]; then
  case "${external_lock}" in
    "${sysroot}" | "${sysroot}/"*)
      echo "Invalid sysroot lock: trust anchor must be outside the sysroot" >&2
      exit 11
      ;;
  esac
  [[ -f "${external_lock}" && ! -L "${external_lock}" ]] || {
    echo "Invalid sysroot lock: external lock is not a regular file" >&2
    exit 11
  }

  content_identity="$(awk 'NR == 1 {print $1}' "${content_checksum}")"
  target_metadata_identity="$(
    sha256sum "${metadata_checksum}" | awk '{print $1}'
  )"
  if ! python3 - \
    "${external_lock}" \
    "$(basename "${sysroot}")" \
    "${content_identity}" \
    "${target_metadata_identity}" <<'PY'
import json
import sys

lock_path, artifact_id, content_sha256, metadata_sha256 = sys.argv[1:]
with open(lock_path, encoding="utf-8") as source:
    document = json.load(source)

expected = {
    "schema_version": 1,
    "artifact_id": artifact_id,
    "architecture": "arm64",
    "os": {"id": "ubuntu", "version_id": "22.04"},
    "sysroot_content_sha256": content_sha256,
    "target_metadata_sha256": metadata_sha256,
}
if document != expected:
    raise SystemExit("external lock does not match the validated sysroot identity")
PY
  then
    echo "Invalid sysroot lock: external identity verification failed" >&2
    exit 11
  fi
fi

printf 'sysroot=%s\nloader=%s\nos=%s %s\ntrust_anchor=%s\n' \
  "${sysroot}" "${loader}" "${os_pretty_name:-Ubuntu}" "${os_version}" \
  "${external_lock:-internal-only}"
