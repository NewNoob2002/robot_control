#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
expected_linux=f1348d4072cdabea4c3435a13c721ac29ab4cc91
expected_node=ef9ac3a2279e34855a20c787fc1bc48bc995ec22
linux_path=components/CANopenLinux
node_path=components/CANopenLinux/CANopenNode
readonly repo_root expected_linux expected_node linux_path node_path

fail() {
  printf '%s\n' "$2" >&2
  exit "$1"
}

[[ -e "${repo_root}/${linux_path}" ]] ||
  fail 2 "CANopenLinux dependency is missing or uninitialized: ${linux_path}"
if ! linux_root="$(
  git -C "${repo_root}/${linux_path}" rev-parse --show-toplevel 2>/dev/null
)"; then
  fail 2 "CANopenLinux dependency is missing or uninitialized: ${linux_path}"
fi
[[ "$(realpath "${linux_root}")" == "$(realpath "${repo_root}/${linux_path}")" ]] ||
  fail 2 "CANopenLinux dependency is missing or uninitialized: ${linux_path}"
[[ -e "${repo_root}/${node_path}" ]] ||
  fail 2 "CANopenNode dependency is missing or uninitialized: ${node_path}"
if ! node_root="$(
  git -C "${repo_root}/${node_path}" rev-parse --show-toplevel 2>/dev/null
)"; then
  fail 2 "CANopenNode dependency is missing or uninitialized: ${node_path}"
fi
[[ "$(realpath "${node_root}")" == "$(realpath "${repo_root}/${node_path}")" ]] ||
  fail 2 "CANopenNode dependency is missing or uninitialized: ${node_path}"

configured_linux_path="$(
  git -C "${repo_root}" config -f .gitmodules \
    --get submodule.components/CANopenLinux.path || true
)"
[[ "${configured_linux_path}" == "${linux_path}" ]] ||
  fail 3 "CANopenLinux path mismatch: ${configured_linux_path:-missing}"

linux_url="$(git -C "${repo_root}" config -f .gitmodules --get submodule.components/CANopenLinux.url || true)"
[[ "${linux_url}" == "https://github.com/CANopenNode/CANopenLinux.git" ]] ||
  fail 3 "CANopenLinux URL mismatch: ${linux_url:-missing}"

read -r root_mode root_revision _ _ < <(
  git -C "${repo_root}" ls-files --stage -- "${linux_path}"
)
[[ "${root_mode:-}" == 160000 && "${root_revision:-}" == "${expected_linux}" ]] ||
  fail 3 "CANopenLinux root gitlink mismatch: mode=${root_mode:-missing} revision=${root_revision:-missing}"

linux_revision="$(git -C "${repo_root}/${linux_path}" rev-parse HEAD)"
[[ "${linux_revision}" == "${expected_linux}" ]] ||
  fail 3 "CANopenLinux checkout mismatch: ${linux_revision}"

read -r node_mode _ node_revision _ < <(
  git -C "${repo_root}/${linux_path}" ls-tree HEAD -- CANopenNode
)
[[ "${node_mode:-}" == 160000 && "${node_revision:-}" == "${expected_node}" ]] ||
  fail 3 "CANopenNode nested gitlink mismatch: mode=${node_mode:-missing} revision=${node_revision:-missing}"

checked_out_node="$(git -C "${repo_root}/${node_path}" rev-parse HEAD)"
[[ "${checked_out_node}" == "${expected_node}" ]] ||
  fail 3 "CANopenNode checkout mismatch: ${checked_out_node}"

node_url="$(git -C "${repo_root}/${linux_path}" config -f .gitmodules --get submodule.CANopenNode.url || true)"
[[ "${node_url}" == "https://github.com/CANopenNode/CANopenNode.git" ]] ||
  fail 3 "CANopenNode URL mismatch: ${node_url:-missing}"

if ! linux_status="$(
  git -C "${repo_root}/${linux_path}" status \
    --porcelain=v1 --untracked-files=all
)"; then
  fail 4 "Unable to determine CANopenLinux dependency cleanliness"
fi
[[ -z "${linux_status}" ]] ||
  fail 4 "CANopenLinux dependency is dirty"
if ! node_status="$(
  git -C "${repo_root}/${node_path}" status \
    --porcelain=v1 --untracked-files=all
)"; then
  fail 4 "Unable to determine CANopenNode dependency cleanliness"
fi
[[ -z "${node_status}" ]] ||
  fail 4 "CANopenNode dependency is dirty"

printf 'dependency=CANopenLinux revision=%s dirty=false\n' "${linux_revision}"
printf 'dependency=CANopenNode revision=%s dirty=false\n' "${checked_out_node}"
printf 'relationship=nested-gitlink result=pass\n'
