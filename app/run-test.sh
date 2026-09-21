#!/usr/bin/env bash
set -euo pipefail
package_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec "${package_dir}/robot-control-app" --config "${package_dir}/rk3588-test.conf" "$@"
