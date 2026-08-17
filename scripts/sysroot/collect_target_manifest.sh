#!/usr/bin/env bash
set -euo pipefail

output="${1:-target-manifest}"
mkdir -p "${output}"

cat /etc/os-release >"${output}/os-release"
uname -a >"${output}/uname.txt"
dpkg --print-architecture >"${output}/architecture.txt"
dpkg-query -W -f='${binary:Package}\t${Version}\t${Architecture}\n' \
  | LC_ALL=C sort >"${output}/packages.tsv"
ldd --version >"${output}/ldd-version.txt" 2>&1
getconf GNU_LIBC_VERSION >"${output}/glibc-version.txt"

find /lib /usr/lib /usr/include \
  -xdev \( -type f -o -type l \) 2>/dev/null \
  | LC_ALL=C sort >"${output}/sysroot-files.txt"

(
  cd "${output}"
  sha256sum \
    architecture.txt \
    glibc-version.txt \
    ldd-version.txt \
    os-release \
    packages.tsv \
    sysroot-files.txt \
    uname.txt \
    >manifest.sha256
)
