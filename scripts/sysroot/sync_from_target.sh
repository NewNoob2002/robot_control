#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <ssh-target> <output-sysroot>" >&2
  exit 2
fi

readonly target="$1"
readonly output="$(mkdir -p "$2" && realpath "$2")"

if [[ "${output}" == "/" ]]; then
  echo "Refusing to use / as a sysroot destination" >&2
  exit 3
fi

mkdir -p "${output}/.robot-control"

for path in lib usr/include usr/lib; do
  rsync --archive --links --numeric-ids \
    --delete-delay \
    "${target}:/${path}/" "${output}/${path}/"
done

# Collect the manifest separately without assuming privileged target writes.
tmp_manifest="$(mktemp -d)"
trap 'rm -rf "${tmp_manifest}"' EXIT
ssh "${target}" 'tmp=$(mktemp -d); bash -s "$tmp"; tar -C "$tmp" -cf - .' \
  <"$(dirname "$0")/collect_target_manifest.sh" \
  | tar -C "${tmp_manifest}" -xf -
cp -a "${tmp_manifest}/." "${output}/.robot-control/"

"$(dirname "$0")/normalize_sysroot.sh" "${output}"
"$(dirname "$0")/validate_sysroot.sh" "${output}"
