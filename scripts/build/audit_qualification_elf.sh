#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C

binary="$(realpath -- "$1")"
sysroot="$(realpath -- "$2")"
readelf_bin=aarch64-linux-gnu-readelf

file "${binary}" | grep -q 'ARM aarch64'
program_headers="$("${readelf_bin}" --program-headers "${binary}")"
dynamic="$("${readelf_bin}" --dynamic "${binary}")"
grep -Eq 'Requesting program interpreter: /lib/(aarch64-linux-gnu/)?ld-linux-aarch64.so.1' <<<"${program_headers}"
if grep -Eq '\((RPATH|RUNPATH)\)' <<<"${dynamic}"; then
    echo "Qualification ELF must not contain RPATH or RUNPATH" >&2
    exit 1
fi

mapfile -t needed < <(sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p' <<<"${dynamic}")
for library in "${needed[@]}"; do
    find "${sysroot}/lib" "${sysroot}/usr/lib" \
        \( -type f -o -type l \) -name "${library}" -print -quit 2>/dev/null | grep -q .
done

version_info="$("${readelf_bin}" --version-info "${binary}")"
check_versions() {
    local prefix="$1"
    local library="$2"
    local required
    local provided
    required="$(grep -Eo "${prefix}_[0-9]+([.][0-9]+)*" <<<"${version_info}" | sort -Vu || true)"
    [[ -n "${required}" ]] || return 0
    provided="$("${readelf_bin}" --version-info "${library}")"
    while IFS= read -r version; do
        grep -Eq "(^|[[:space:]])${version}([[:space:]]|$)" <<<"${provided}"
    done <<<"${required}"
}

check_versions GLIBC "${sysroot}/lib/aarch64-linux-gnu/libc.so.6"
check_versions GLIBCXX "${sysroot}/usr/lib/aarch64-linux-gnu/libstdc++.so.6"
check_versions CXXABI "${sysroot}/usr/lib/aarch64-linux-gnu/libstdc++.so.6"

printf 'elf=%s\ninterpreter=validated\nneeded=%s\nsymbol_versions=validated\nrpath=none\n' \
    "${binary}" "${needed[*]}"
