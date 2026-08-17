#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <sysroot>" >&2
  exit 2
fi

sysroot="$(realpath -- "$1")"
readonly sysroot

if [[ "${sysroot}" == "/" ]]; then
  echo "Refusing to normalize / as a sysroot" >&2
  exit 3
fi

for required_path in lib usr/include usr/lib; do
  if [[ ! -d "${sysroot}/${required_path}" ]]; then
    echo "Invalid sysroot: ${required_path} is missing" >&2
    exit 3
  fi
done

readonly report="${sysroot}/.robot-control/normalization.tsv"
mkdir -p "$(dirname "${report}")"
: >"${report}"

while IFS= read -r -d '' link; do
  target="$(readlink "${link}")"
  if [[ "${target}" == /* ]]; then
    relative_target="$(
      realpath \
        --canonicalize-missing \
        --no-symlinks \
        --relative-to="$(dirname "${link}")" \
        "${sysroot}${target}"
    )"
    printf '%s\t%s\t%s\n' "${link#"${sysroot}"/}" "${target}" "${relative_target}" \
      >>"${report}"
    ln -sfn "${relative_target}" "${link}"
  fi
done < <(
  find \
    "${sysroot}/lib" \
    "${sysroot}/usr/lib" \
    "${sysroot}/usr/include" \
    -type l -print0
)
