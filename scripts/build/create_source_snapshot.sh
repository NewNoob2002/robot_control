#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <snapshot-directory> <attestation-json>" >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
snapshot_parent="$(dirname "$1")"
mkdir -p -- "${snapshot_parent}"
snapshot="$(realpath --canonicalize-missing -- "$1")"
attestation_parent="$(dirname "$2")"
mkdir -p -- "${attestation_parent}"
attestation="$(realpath --canonicalize-missing -- "$2")"
readonly repo_root snapshot attestation

case "${snapshot}" in
  "${repo_root}/out/"*) ;;
  *)
    echo "Source snapshots must be created below ${repo_root}/out" >&2
    exit 3
    ;;
esac

if [[ -e "${snapshot}" ]]; then
  echo "Source snapshot destination already exists: ${snapshot}" >&2
  exit 3
fi

temporary="$(mktemp -d "${repo_root}/out/.source-snapshot.XXXXXX")"
readonly temporary

# Remove only the private temporary directory created above.
cleanup() {
  python3 - "${temporary}" <<'PY'
import shutil
import sys

shutil.rmtree(sys.argv[1], ignore_errors=True)
PY
}
trap cleanup EXIT

readonly file_list="${temporary}/files.zlist"
readonly archive="${temporary}/source.tar"

git -C "${repo_root}" ls-files \
  --cached \
  --others \
  --exclude-standard \
  -z |
  while IFS= read -r -d '' path; do
    if [[ -e "${repo_root}/${path}" || -L "${repo_root}/${path}" ]]; then
      printf '%s\0' "${path}"
    fi
  done |
  LC_ALL=C sort -z >"${file_list}"

if [[ ! -s "${file_list}" ]]; then
  echo "No source files were selected for the build snapshot" >&2
  exit 4
fi

(
  cd "${repo_root}"
  tar \
    --create \
    --file="${archive}" \
    --format=gnu \
    --null \
    --no-recursion \
    --files-from="${file_list}" \
    --mtime="@0" \
    --owner=0 \
    --group=0 \
    --numeric-owner
)

snapshot_sha256="$(sha256sum "${archive}" | awk '{print $1}')"
source_revision="$(git -C "${repo_root}" rev-parse HEAD)"
source_dirty=false
if [[ -n "$(git -C "${repo_root}" status --porcelain=v1)" ]]; then
  source_dirty=true
fi
file_count="$(
  python3 - "${file_list}" <<'PY'
import sys

with open(sys.argv[1], "rb") as source:
    entries = [entry for entry in source.read().split(b"\0") if entry]
print(len(entries))
PY
)"

mkdir -- "${snapshot}"
tar --extract --file="${archive}" --directory="${snapshot}" --no-same-owner

python3 - \
  "${attestation}" \
  "${source_revision}" \
  "${source_dirty}" \
  "${snapshot_sha256}" \
  "${file_count}" <<'PY'
import json
import sys

output, revision, dirty, snapshot_sha256, file_count = sys.argv[1:]
document = {
    "schema_version": 1,
    "revision": revision,
    "dirty": dirty == "true",
    "snapshot_sha256": snapshot_sha256,
    "file_count": int(file_count),
}
with open(output, "w", encoding="utf-8") as stream:
    json.dump(document, stream, indent=2, sort_keys=True)
    stream.write("\n")
PY

printf 'snapshot=%s\nattestation=%s\nsha256=%s\nfiles=%s\n' \
  "${snapshot}" "${attestation}" "${snapshot_sha256}" "${file_count}"
