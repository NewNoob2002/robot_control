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

if ssh "${target}" 'command -v rsync >/dev/null 2>&1'; then
  for path in lib usr/include usr/lib; do
    rsync --archive --links --numeric-ids \
      --delete-delay \
      "${target}:/${path}/" "${output}/${path}/"
  done
else
  staging="$(mktemp -d "${output}.staging.XXXXXX")"
  trap 'rm -rf "${tmp_manifest:-}" "${staging:-}"' EXIT
  ssh "${target}" \
    'tar --create --file=- --numeric-owner --one-file-system --directory=/ lib usr/include usr/lib' \
    | tar --extract --file=- --no-same-owner --directory="${staging}"
  rm -rf "${output}/lib" "${output}/usr/include" "${output}/usr/lib"
  mkdir -p "${output}/usr"
  mv "${staging}/lib" "${output}/lib"
  mv "${staging}/usr/include" "${output}/usr/include"
  mv "${staging}/usr/lib" "${output}/usr/lib"
  rm -rf "${staging}"
fi

# Collect the manifest separately without assuming privileged target writes.
tmp_manifest="$(mktemp -d)"
trap 'rm -rf "${tmp_manifest}"' EXIT
ssh "${target}" 'tmp=$(mktemp -d); bash -s "$tmp"; tar -C "$tmp" -cf - .' \
  <"$(dirname "$0")/collect_target_manifest.sh" \
  | tar -C "${tmp_manifest}" -xf -
cp -a "${tmp_manifest}/." "${output}/.robot-control/"

"$(dirname "$0")/normalize_sysroot.sh" "${output}"
"$(dirname "$0")/validate_sysroot.sh" "${output}"
