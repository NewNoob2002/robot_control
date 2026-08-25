#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly repo_root
"${repo_root}/scripts/build/verify_canopen_dependencies.sh"

cmake --preset host-test -S "${repo_root}"
cmake --build --preset host-test
ctest --preset host-test --exclude-regex '^socketcan_(socket_lifecycle|vcan_managed)$'
