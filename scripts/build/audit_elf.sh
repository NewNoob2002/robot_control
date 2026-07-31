#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <aarch64-elf> <sysroot>" >&2
  exit 2
fi

readonly binary="$(realpath "$1")"
readonly sysroot="$(realpath "$2")"
if [[ -n "${READELF:-}" ]]; then
  readonly readelf_bin="${READELF}"
elif command -v aarch64-linux-gnu-readelf >/dev/null 2>&1; then
  readonly readelf_bin="aarch64-linux-gnu-readelf"
else
  readonly readelf_bin="readelf"
fi

file "${binary}" | grep -q 'ARM aarch64' || {
  echo "Not an aarch64 ELF: ${binary}" >&2
  exit 3
}

program_headers="$("${readelf_bin}" --program-headers "${binary}")"
dynamic="$("${readelf_bin}" --dynamic "${binary}")"

grep -Eq 'Requesting program interpreter: /lib/(aarch64-linux-gnu/)?ld-linux-aarch64.so.1' \
  <<<"${program_headers}" || {
  echo "Unexpected aarch64 ELF interpreter" >&2
  exit 4
}

if grep -Eq '\((RPATH|RUNPATH)\)' <<<"${dynamic}"; then
  echo "Target binary contains RPATH/RUNPATH" >&2
  exit 5
fi

mapfile -t needed < <(sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p' <<<"${dynamic}")
for library in "${needed[@]}"; do
  if ! find "${sysroot}/lib" "${sysroot}/usr/lib" \
      \( -type f -o -type l \) -name "${library}" -print -quit 2>/dev/null \
      | grep -q .; then
    echo "Dependency not found in sysroot: ${library}" >&2
    exit 6
  fi
done

version_info="$("${readelf_bin}" --version-info "${binary}")"
symbols="$("${readelf_bin}" --wide --symbols "${binary}")"

for symbol in pthread_sigmask signalfd ppoll clock_nanosleep tcsetattr elog_output; do
  grep -Eq "[[:space:]]${symbol}(@|$)" <<<"${symbols}" || {
    echo "Required Phase 3 symbol is not linked: ${symbol}" >&2
    exit 8
  }
done

check_versions() {
  local prefix="$1"
  local library="$2"
  local provided
  local required

  required="$(grep -Eo "${prefix}_[0-9]+([.][0-9]+)*" <<<"${version_info}" \
    | sort -Vu || true)"
  [[ -n "${required}" ]] || return 0

  provided="$("${readelf_bin}" --version-info "${library}")"
  while IFS= read -r version; do
    grep -Eq "(^|[[:space:]])${version}([[:space:]]|$)" <<<"${provided}" || {
      echo "Required symbol version unavailable in sysroot: ${version}" >&2
      exit 7
    }
  done <<<"${required}"
}

check_versions GLIBC "${sysroot}/lib/aarch64-linux-gnu/libc.so.6"
check_versions GLIBCXX "${sysroot}/usr/lib/aarch64-linux-gnu/libstdc++.so.6"
check_versions CXXABI "${sysroot}/usr/lib/aarch64-linux-gnu/libstdc++.so.6"

printf 'elf=%s\ninterpreter=validated\nneeded=%s\nsymbol_versions=validated\nphase3_symbols=validated\nrpath=none\n' \
  "${binary}" "${needed[*]}"
