#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
verifier="${repo_root}/scripts/build/verify_canopen_dependencies.sh"
marker="${repo_root}/components/CANopenLinux/CANopenNode/.robot-control-dirty-test-$$"
wrong_revision_verifier="$(mktemp "${repo_root}/scripts/build/.verify-canopen-wrong-revision.XXXXXX")"
temp="$(mktemp -d)"
readonly repo_root verifier marker wrong_revision_verifier temp

cleanup() {
  rm -f -- "${marker}"
  rm -f -- "${wrong_revision_verifier}"
  rm -rf -- "${temp}"
}
trap cleanup EXIT

"${verifier}"

if grep -q 'ROBOT_CONTROL_TEST_EXPECTED_CANOPEN_LINUX' "${verifier}"; then
  echo "Production verifier still permits an expected-revision override" >&2
  exit 1
fi

[[ ! -e "${repo_root}/components/CANopenNode" ]] || {
  echo "Legacy CANopenNode snapshot still exists" >&2
  exit 1
}

cp "${verifier}" "${wrong_revision_verifier}"
sed -i \
  's/f1348d4072cdabea4c3435a13c721ac29ab4cc91/0000000000000000000000000000000000000000/' \
  "${wrong_revision_verifier}"
chmod +x "${wrong_revision_verifier}"
set +e
"${wrong_revision_verifier}" >/dev/null 2>&1
status=$?
set -e
[[ ${status} -eq 3 ]] || {
  echo "Wrong CANopenLinux revision returned ${status}, expected 3" >&2
  exit 1
}

[[ ! -e "${marker}" ]] || {
  echo "Dirty-tree test marker already exists: ${marker}" >&2
  exit 1
}
: >"${marker}"
set +e
"${verifier}" >/dev/null 2>&1
status=$?
set -e
[[ ${status} -eq 4 ]] || {
  echo "Dirty CANopenNode tree returned ${status}, expected 4" >&2
  exit 1
}
rm -f -- "${marker}"

readonly fixture_source="${temp}/fixture-source"
readonly fixture_checkout="${temp}/fixture-checkout"
mkdir -p "${fixture_source}/scripts/build"
cp "${verifier}" "${fixture_source}/scripts/build/verify_canopen_dependencies.sh"
git init -q "${fixture_source}"
git -C "${fixture_source}" config user.name "Robot Control Test"
git -C "${fixture_source}" config user.email \
  "robot-control-test@example.invalid"
git -C "${fixture_source}" -c protocol.file.allow=always submodule add \
  "${repo_root}/components/CANopenLinux" components/CANopenLinux >/dev/null
git -C "${fixture_source}" add scripts/build/verify_canopen_dependencies.sh
git -C "${fixture_source}" commit -qm 'uninitialized submodule fixture'
git clone -q "${fixture_source}" "${fixture_checkout}"
git -C "${fixture_checkout}" config -f .gitmodules \
  submodule.components/CANopenLinux.url \
  https://github.com/CANopenNode/CANopenLinux.git
mkdir -p "${fixture_checkout}/components/CANopenLinux/CANopenNode"
set +e
"${fixture_checkout}/scripts/build/verify_canopen_dependencies.sh" \
  >"${temp}/uninitialized.log" 2>&1
status=$?
set -e
[[ ${status} -eq 2 ]] || {
  echo "Uninitialized submodule fixture returned ${status}, expected 2" >&2
  exit 1
}

readonly fake_bin="${temp}/fake-bin"
mkdir -p "${fake_bin}"
cat >"${fake_bin}/git" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
if [[ "${ROBOT_CONTROL_TEST_GIT_MODE:-}" == status-error &&
      " $* " == *' status --porcelain=v1 --untracked-files=all '* ]]; then
  exit 19
fi
if [[ "${ROBOT_CONTROL_TEST_GIT_MODE:-}" == wrong-path &&
      " $* " == *' config -f .gitmodules --get submodule.components/CANopenLinux.path '* ]]; then
  echo components/WrongPath
  exit 0
fi
exec "${ROBOT_CONTROL_TEST_REAL_GIT}" "$@"
EOF
chmod +x "${fake_bin}/git"
readonly real_git="$(command -v git)"

set +e
PATH="${fake_bin}:${PATH}" ROBOT_CONTROL_TEST_REAL_GIT="${real_git}" \
  ROBOT_CONTROL_TEST_GIT_MODE=status-error "${verifier}" \
  >"${temp}/status-error.log" 2>&1
status=$?
set -e
[[ ${status} -eq 4 ]] || {
  echo "Dependency status error returned ${status}, expected 4" >&2
  exit 1
}
if grep -q 'dirty=false' "${temp}/status-error.log"; then
  echo "Dependency status error was reported as clean" >&2
  exit 1
fi

set +e
PATH="${fake_bin}:${PATH}" ROBOT_CONTROL_TEST_REAL_GIT="${real_git}" \
  ROBOT_CONTROL_TEST_GIT_MODE=wrong-path "${verifier}" \
  >"${temp}/wrong-path.log" 2>&1
status=$?
set -e
[[ ${status} -eq 3 ]] || {
  echo "Wrong CANopenLinux submodule path returned ${status}, expected 3" >&2
  exit 1
}

grep -q 'scripts/build/verify_canopen_dependencies.sh' \
  "${repo_root}/scripts/build/build_host.sh"
grep -q 'scripts/build/verify_canopen_dependencies.sh' \
  "${repo_root}/scripts/build/build_rk3588.sh"
grep -q 'submodules: recursive' "${repo_root}/.github/workflows/ci.yml"
grep -q './scripts/test/test_canopen_dependencies.sh' \
  "${repo_root}/.github/workflows/ci.yml"

echo "CANopen dependency regression checks passed"
