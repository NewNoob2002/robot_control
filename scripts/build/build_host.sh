#!/usr/bin/env bash
set -euo pipefail

readonly repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

cmake --preset host-test -S "${repo_root}"
cmake --build --preset host-test
ctest --preset host-test

