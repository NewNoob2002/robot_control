#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
temp="$(mktemp -d)"
readonly repo_root temp
trap 'rm -rf "${temp}"' EXIT

while IFS= read -r script; do
  bash -n "${script}"
done < <(find "${repo_root}/scripts" -type f -name '*.sh' -print | LC_ALL=C sort)

if ROBOT_CONTROL_SYSROOT="${temp}" \
  cmake --preset rk3588-debug \
    -S "${repo_root}" \
    -B "${temp}/invalid-cross-build" \
    >"${temp}/cmake.log" 2>&1; then
  echo "Cross configure unexpectedly accepted an invalid sysroot" >&2
  exit 1
fi
grep -Eq 'usr/include is missing|dynamic loader missing' "${temp}/cmake.log"

if "${repo_root}/scripts/sysroot/validate_sysroot.sh" "${temp}" \
  >"${temp}/validate.log" 2>&1; then
  echo "Sysroot validator unexpectedly accepted an invalid sysroot" >&2
  exit 1
fi
grep -q 'usr/include is missing' "${temp}/validate.log"

if "${repo_root}/scripts/sysroot/normalize_sysroot.sh" / \
  >"${temp}/normalize-root.log" 2>&1; then
  echo "Sysroot normalizer unexpectedly accepted /" >&2
  exit 1
fi
grep -q 'Refusing to normalize / as a sysroot' "${temp}/normalize-root.log"

if "${repo_root}/scripts/sysroot/normalize_sysroot.sh" "${temp}" \
  >"${temp}/normalize-invalid.log" 2>&1; then
  echo "Sysroot normalizer unexpectedly accepted an incomplete tree" >&2
  exit 1
fi
grep -q 'Invalid sysroot: lib is missing' "${temp}/normalize-invalid.log"

grep -q 'sysroot-content.sha256' \
  "${repo_root}/scripts/build/write_build_metadata.sh"
if grep -q 'content-manifest.sha256' \
  "${repo_root}/scripts/build/write_build_metadata.sh"; then
  echo "Build metadata still references the obsolete manifest name" >&2
  exit 1
fi

grep -q '^export LC_ALL=C$' "${repo_root}/scripts/build/audit_elf.sh"

grep -q -- '--provenance=false' \
  "${repo_root}/scripts/build/build_cross_image.sh"

# shellcheck disable=SC2016
grep -q -- '"${image_id}"' \
  "${repo_root}/scripts/build/build_rk3588.sh"
grep -q -- 'create_source_snapshot.sh' \
  "${repo_root}/scripts/build/build_rk3588.sh"
grep -q -- 'reset_cross_build_dir.sh' \
  "${repo_root}/scripts/build/build_rk3588.sh"
grep -q -- 'ROBOT_CONTROL_SYSROOT_LOCK' \
  "${repo_root}/scripts/build/build_rk3588.sh"
grep -q -- 'sysroot_lock_sha256' \
  "${repo_root}/scripts/build/setup_cross_container.sh"

snapshot="${repo_root}/out/test-source-snapshot-$$"
attestation="${temp}/source-attestation.json"
"${repo_root}/scripts/build/create_source_snapshot.sh" \
  "${snapshot}" "${attestation}" >/dev/null
test -f "${snapshot}/CMakeLists.txt"
test -d "${snapshot}/out"
test ! -e "${snapshot}/.codex"
grep -q '/opt/robot-control/sysroot' \
  "${repo_root}/docker/cross/Dockerfile"
grep -q 'test -d /opt/robot-control/sysroot' \
  "${repo_root}/scripts/build/verify_cross_image.sh"
grep -q '^/.codex/$' "${repo_root}/.gitignore"
grep -q 'Docker CLI is required to build the locked cross image' \
  "${repo_root}/scripts/build/build_cross_image.sh"
grep -q 'Docker CLI is required to verify the locked cross image' \
  "${repo_root}/scripts/build/verify_cross_image.sh"
python3 - "${attestation}" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as source:
    attestation = json.load(source)
assert attestation["schema_version"] == 1
assert len(attestation["revision"]) == 40
assert isinstance(attestation["dirty"], bool)
assert len(attestation["snapshot_sha256"]) == 64
assert attestation["file_count"] > 0
PY
python3 - "${snapshot}" <<'PY'
import shutil
import sys

shutil.rmtree(sys.argv[1])
PY

readonly snapshot_fixture="${temp}/snapshot-fixture"
mkdir -p "${snapshot_fixture}/scripts/build"
cp "${repo_root}/scripts/build/create_source_snapshot.sh" \
  "${snapshot_fixture}/scripts/build/create_source_snapshot.sh"
cp "${repo_root}/scripts/build/reset_cross_build_dir.sh" \
  "${snapshot_fixture}/scripts/build/reset_cross_build_dir.sh"
(
  cd "${snapshot_fixture}"
  git init -q
  git config user.name "Robot Control Test"
  git config user.email "robot-control-test@example.invalid"
  printf '/out/\n' >.gitignore
  printf 'deleted after commit\n' >deleted-tracked.txt
  git add \
    .gitignore \
    deleted-tracked.txt \
    scripts/build/create_source_snapshot.sh \
    scripts/build/reset_cross_build_dir.sh
  git commit -qm 'snapshot fixture'
  rm deleted-tracked.txt
  printf 'present untracked file\n' >present-untracked.txt
)
"${snapshot_fixture}/scripts/build/create_source_snapshot.sh" \
  "${snapshot_fixture}/out/source" \
  "${snapshot_fixture}/out/source-attestation.json" >/dev/null
test ! -e "${snapshot_fixture}/out/source/deleted-tracked.txt"
test -f "${snapshot_fixture}/out/source/present-untracked.txt"

readonly reset_build_root="${snapshot_fixture}/out/build/cross"
mkdir -p "${reset_build_root}/rk3588-debug"
printf 'stale object\n' >"${reset_build_root}/rk3588-debug/stale-object.o"
"${snapshot_fixture}/scripts/build/reset_cross_build_dir.sh" \
  rk3588-debug >/dev/null
test -d "${reset_build_root}/rk3588-debug"
test ! -e "${reset_build_root}/rk3588-debug/stale-object.o"

printf 'must survive failed reset\n' \
  >"${reset_build_root}/rk3588-debug/stale-object.o"
readonly reset_fake_bin="${temp}/reset-fake-bin"
mkdir -p "${reset_fake_bin}"
cat >"${reset_fake_bin}/python3" <<'EOF'
#!/usr/bin/env bash
exit 19
EOF
chmod +x "${reset_fake_bin}/python3"
if PATH="${reset_fake_bin}:${PATH}" \
  "${snapshot_fixture}/scripts/build/reset_cross_build_dir.sh" \
  rk3588-debug >"${temp}/reset-failure.log" 2>&1; then
  echo "Cross-build reset unexpectedly ignored deletion failure" >&2
  exit 1
fi
test -f "${reset_build_root}/rk3588-debug/stale-object.o"

if ROBOT_CONTROL_SYSROOT_LOCK="${temp}/reviewed-lock.json" \
  ROBOT_CONTROL_SYSROOT_ALLOWED_ROOTS="${temp}" \
  "${repo_root}/scripts/sysroot/sync_from_target.sh" \
  invalid-target "${temp}/test-sync-lock" \
  >"${temp}/sync-lock-output.log" 2>&1; then
  echo "Sysroot sync unexpectedly accepted the build-only lock variable" >&2
  exit 1
fi
grep -q 'build-only' "${temp}/sync-lock-output.log"
if find "${temp}" -maxdepth 1 \
  \( -name '.test-sync-lock.staging.*' \
  -o -name '.test-sync-lock.lock.json.staging.*' \) \
  -print -quit | grep -q .; then
  echo "Sysroot sync leaked staging resources after early rejection" >&2
  exit 1
fi

readonly forbidden_sync_output="${repo_root}/sysroots/locks/test-sync-$$"
if ROBOT_CONTROL_SYSROOT_ALLOWED_ROOTS="${repo_root}/sysroots" \
  "${repo_root}/scripts/sysroot/sync_from_target.sh" \
  invalid-target "${forbidden_sync_output}" \
  >"${temp}/sync-reviewed-lock-output.log" 2>&1; then
  echo "Sysroot sync unexpectedly accepted reviewed-lock storage" >&2
  exit 1
fi
grep -q 'reviewed-lock storage' "${temp}/sync-reviewed-lock-output.log"
test ! -e "${forbidden_sync_output}"
test ! -e "${forbidden_sync_output}.lock.json"

readonly orphaned_sync_output="${temp}/orphaned-sysroot"
printf 'unmanaged adjacent file\n' >"${orphaned_sync_output}.lock.json"
if ROBOT_CONTROL_SYSROOT_ALLOWED_ROOTS="${temp}" \
  "${repo_root}/scripts/sysroot/sync_from_target.sh" \
  invalid-target "${orphaned_sync_output}" \
  >"${temp}/sync-orphaned-lock.log" 2>&1; then
  echo "Sysroot sync unexpectedly accepted an orphaned adjacent lock" >&2
  exit 1
fi
grep -q 'orphaned sysroot lock' "${temp}/sync-orphaned-lock.log"
grep -q 'unmanaged adjacent file' "${orphaned_sync_output}.lock.json"

if ROBOT_CONTROL_SYSROOT="${temp}" \
  ROBOT_CONTROL_PRESET=rk3588-release \
  "${repo_root}/scripts/build/build_rk3588.sh" \
  >"${temp}/release-lock.log" 2>&1; then
  echo "Release build unexpectedly accepted an implicit sysroot lock" >&2
  exit 1
fi
grep -q 'requires an explicit reviewed ROBOT_CONTROL_SYSROOT_LOCK' \
  "${temp}/release-lock.log"

readonly caller_lock_target="${temp}/caller-lock-target.json"
readonly debug_lock_symlink="${temp}/debug-lock-symlink.json"
printf '{"schema_version":1}\n' >"${caller_lock_target}"
ln -s "${caller_lock_target}" "${debug_lock_symlink}"
if ROBOT_CONTROL_SYSROOT="${temp}" \
  ROBOT_CONTROL_PRESET=rk3588-debug \
  ROBOT_CONTROL_SYSROOT_LOCK="${debug_lock_symlink}" \
  "${repo_root}/scripts/build/build_rk3588.sh" \
  >"${temp}/debug-lock-symlink.log" 2>&1; then
  echo "Debug build unexpectedly accepted a caller-supplied lock symlink" >&2
  exit 1
fi
grep -q 'Debug sysroot lock must be a regular non-symlink file' \
  "${temp}/debug-lock-symlink.log"

readonly artifact_workspace="${temp}/artifact-workspace"
readonly artifact_preset="rk3588-release"
readonly artifact_output="${artifact_workspace}/out/artifacts/${artifact_preset}"
readonly positive_artifact_preset="rk3588-debug"
readonly positive_artifact_output="${artifact_workspace}/out/artifacts/${positive_artifact_preset}"
readonly artifact_sysroot="${temp}/artifact-sysroot"
readonly artifact_metadata="${artifact_sysroot}/.robot-control"
readonly artifact_lock="${temp}/artifact-sysroot.lock.json"
readonly artifact_binary="${temp}/artifact-binary"
readonly artifact_attestation="${temp}/artifact-source-attestation.json"
readonly artifact_source_snapshot="${temp}/artifact-source-snapshot"
readonly fake_bin="${temp}/fake-bin"
mkdir -p \
  "${artifact_metadata}" \
  "${artifact_output}/.robot-control" \
  "${artifact_source_snapshot}/cmake/toolchains" \
  "${artifact_source_snapshot}/docker/cross" \
  "${fake_bin}"
printf 'previous publication\n' >"${artifact_output}/old-marker"
printf 'robot-control-artifact-v1\n' \
  >"${artifact_output}/.robot-control/managed-artifact"
printf '%064d  sysroot-content.jsonl\n' 0 \
  >"${artifact_metadata}/sysroot-content.sha256"
printf 'target metadata\n' >"${artifact_metadata}/manifest.sha256"
printf '{"schema_version":1}\n' >"${artifact_lock}"
printf 'artifact\n' >"${artifact_binary}"
printf 'fixture presets frozen in source snapshot\n' \
  >"${artifact_source_snapshot}/CMakePresets.json"
printf 'fixture toolchain frozen in source snapshot\n' \
  >"${artifact_source_snapshot}/cmake/toolchains/aarch64-rk3588-ubuntu2204.cmake"
cp \
  "${repo_root}/docker/cross/Dockerfile" \
  "${repo_root}/docker/cross/packages.lock" \
  "${repo_root}/docker/cross/image.lock" \
  "${artifact_source_snapshot}/docker/cross/"
cat >"${artifact_attestation}" <<'EOF'
{
  "dirty": true,
  "file_count": 1,
  "revision": "0000000000000000000000000000000000000000",
  "schema_version": 1,
  "snapshot_sha256": "0000000000000000000000000000000000000000000000000000000000000000"
}
EOF

cat >"${fake_bin}/docker" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' \
  'aarch64-linux-gnu-g++ fixture' \
  'GNU ld fixture' \
  'cmake version fixture' \
  'ninja fixture'
EOF
cat >"${fake_bin}/mv" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

arguments=("$@")
source_path="${arguments[${#arguments[@]}-2]}"
destination="${arguments[${#arguments[@]}-1]}"
if [[ "${destination}" == "${TEST_ARTIFACT_OUTPUT}" &&
  "${source_path}" == *".staging."* ]]; then
  exit 1
fi
exec /bin/mv "$@"
EOF
chmod +x "${fake_bin}/docker" "${fake_bin}/mv"

content_sha256="$(awk 'NR == 1 {print $1}' \
  "${artifact_metadata}/sysroot-content.sha256")"
target_metadata_sha256="$(
  sha256sum "${artifact_metadata}/manifest.sha256" | awk '{print $1}'
)"
artifact_lock_sha256="$(sha256sum "${artifact_lock}" | awk '{print $1}')"

PATH="${fake_bin}:${PATH}" \
  TEST_ARTIFACT_OUTPUT="${artifact_output}" \
  ROBOT_CONTROL_WORKSPACE_ROOT="${artifact_workspace}" \
  "${repo_root}/scripts/build/write_build_metadata.sh" \
  "${positive_artifact_preset}" \
  "${artifact_binary}" \
  "${artifact_sysroot}" \
  "${artifact_attestation}" \
  "${content_sha256}" \
  "${target_metadata_sha256}" \
  "${artifact_lock}" \
  "${artifact_lock_sha256}" \
  "${artifact_source_snapshot}" \
  >"${temp}/artifact-positive.log"
test -f "${positive_artifact_output}/artifact-binary"
test -f "${positive_artifact_output}/SHA256SUMS"
grep -qx 'robot-control-artifact-v1' \
  "${positive_artifact_output}/.robot-control/managed-artifact"
(
  cd "${positive_artifact_output}"
  sha256sum --check SHA256SUMS >/dev/null
)
python3 - \
  "${positive_artifact_output}/build-metadata.json" \
  "${artifact_lock_sha256}" \
  "${artifact_source_snapshot}/CMakePresets.json" \
  "${artifact_source_snapshot}/cmake/toolchains/aarch64-rk3588-ubuntu2204.cmake" <<'PY'
import hashlib
import json
import sys

metadata_path, lock_sha256, presets_path, toolchain_path = sys.argv[1:]
with open(metadata_path, encoding="utf-8") as source:
    metadata = json.load(source)
assert metadata["schema_version"] == 2
assert metadata["artifact"]["path"] == "artifact-binary"
assert metadata["sysroot"]["external_lock_sha256"] == lock_sha256
assert metadata["source"]["dirty"] is True
with open(presets_path, "rb") as source:
    expected_presets = hashlib.sha256(source.read()).hexdigest()
with open(toolchain_path, "rb") as source:
    expected_toolchain = hashlib.sha256(source.read()).hexdigest()
assert metadata["build"]["cmake_presets_sha256"] == expected_presets
assert metadata["build"]["toolchain_sha256"] == expected_toolchain
PY

if PATH="${fake_bin}:${PATH}" \
  TEST_ARTIFACT_OUTPUT="${artifact_output}" \
  ROBOT_CONTROL_WORKSPACE_ROOT="${artifact_workspace}" \
  "${repo_root}/scripts/build/write_build_metadata.sh" \
  "${artifact_preset}" \
  "${artifact_binary}" \
  "${artifact_sysroot}" \
  "${artifact_attestation}" \
  "${content_sha256}" \
  "${target_metadata_sha256}" \
  "${artifact_lock}" \
  "${artifact_lock_sha256}" \
  "${artifact_source_snapshot}" \
  >"${temp}/artifact-promotion.log" 2>&1; then
  echo "Artifact publication unexpectedly succeeded after promotion failure" >&2
  exit 1
fi
test -f "${artifact_output}/old-marker"

if PATH="${fake_bin}:${PATH}" \
  ROBOT_CONTROL_WORKSPACE_ROOT="${artifact_workspace}" \
  "${repo_root}/scripts/build/write_build_metadata.sh" \
  ../escaped \
  "${artifact_binary}" \
  "${artifact_sysroot}" \
  "${artifact_attestation}" \
  "${content_sha256}" \
  "${target_metadata_sha256}" \
  "${artifact_lock}" \
  "${artifact_lock_sha256}" \
  "${artifact_source_snapshot}" \
  >"${temp}/artifact-path-escape.log" 2>&1; then
  echo "Artifact metadata unexpectedly accepted a path-escaping preset" >&2
  exit 1
fi
grep -q 'Unsupported artifact preset' "${temp}/artifact-path-escape.log"
if find "${artifact_workspace}/out/artifacts" -maxdepth 1 -type d \
  \( -name '.rk3588-debug.staging.*' \
  -o -name '.rk3588-debug.previous.*' \
  -o -name '.rk3588-release.staging.*' \
  -o -name '.rk3588-release.previous.*' \) \
  -print -quit | grep -q .; then
  echo "Artifact rollback leaked a staging or backup directory" >&2
  exit 1
fi

rm -rf -- "${positive_artifact_output}"
mkdir -p -- "${positive_artifact_output}"
printf 'foreign publication\n' >"${positive_artifact_output}/foreign"
if PATH="${fake_bin}:${PATH}" \
  TEST_ARTIFACT_OUTPUT="${artifact_output}" \
  ROBOT_CONTROL_WORKSPACE_ROOT="${artifact_workspace}" \
  "${repo_root}/scripts/build/write_build_metadata.sh" \
  "${positive_artifact_preset}" \
  "${artifact_binary}" \
  "${artifact_sysroot}" \
  "${artifact_attestation}" \
  "${content_sha256}" \
  "${target_metadata_sha256}" \
  "${artifact_lock}" \
  "${artifact_lock_sha256}" \
  "${artifact_source_snapshot}" \
  >"${temp}/artifact-unmanaged.log" 2>&1; then
  echo "Artifact publication unexpectedly replaced an unmanaged directory" >&2
  exit 1
fi
grep -q 'unmanaged artifact output' "${temp}/artifact-unmanaged.log"
grep -q 'foreign publication' "${positive_artifact_output}/foreign"

readonly container_fixture="${temp}/container-fixture"
readonly container_fake_bin="${temp}/container-fake-bin"
mkdir -p \
  "${container_fixture}/scripts/build" \
  "${container_fixture}/docker/cross" \
  "${container_fake_bin}"
cp "${repo_root}/scripts/build/setup_cross_container.sh" \
  "${container_fixture}/scripts/build/setup_cross_container.sh"
cat >"${container_fixture}/scripts/build/verify_cross_image.sh" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
chmod +x \
  "${container_fixture}/scripts/build/setup_cross_container.sh" \
  "${container_fixture}/scripts/build/verify_cross_image.sh"
cat >"${container_fixture}/docker/cross/image.lock" <<'EOF'
image_name=rk3588-cross
image_tag=fixture
image_id=sha256:fixture
container_name=rk3588-dev
EOF
readonly container_lock_symlink="${temp}/container-lock-symlink.json"
ln -s "${caller_lock_target}" "${container_lock_symlink}"
if PATH="${container_fake_bin}:${PATH}" \
  FAKE_DOCKER_LOG="${temp}/container-lock-symlink-docker.log" \
  ROBOT_CONTROL_SYSROOT="${temp}" \
  ROBOT_CONTROL_SYSROOT_LOCK="${container_lock_symlink}" \
  "${container_fixture}/scripts/build/setup_cross_container.sh" \
  >"${temp}/container-lock-symlink.log" 2>&1; then
  echo "Container setup unexpectedly accepted a caller-supplied lock symlink" >&2
  exit 1
fi
grep -q 'Container sysroot lock must be a regular non-symlink file' \
  "${temp}/container-lock-symlink.log"
test ! -e "${temp}/container-lock-symlink-docker.log"

cat >"${container_fake_bin}/docker" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

if [[ "${1:-}" == "container" && "${2:-}" == "inspect" ]]; then
  if [[ "$*" != *"--format"* ]]; then
    exit 0
  fi
  case "$*" in
    *robot-control.managed*) printf '%s\n' "${FAKE_MANAGED:-}" ;;
    *robot-control.repository-sha256*) printf '%s\n' "${FAKE_REPOSITORY:-}" ;;
    *robot-control.clone-id*) printf '%s\n' "${FAKE_CLONE_ID:-}" ;;
    *robot-control.owner-uid*) printf '%s\n' "${FAKE_OWNER_UID:-}" ;;
    *robot-control.owner-gid*) printf '%s\n' "${FAKE_OWNER_GID:-}" ;;
    *robot-control.container-contract*) printf '%s\n' "${FAKE_CONTRACT:-}" ;;
  esac
  exit 0
fi
if [[ "${1:-}" == "rm" ]]; then
  printf 'removed\n' >>"${FAKE_DOCKER_LOG}"
  exit 0
fi
echo "Unexpected fake docker invocation: $*" >&2
exit 97
EOF
chmod +x "${container_fake_bin}/docker"
readonly fake_docker_log="${temp}/fake-docker.log"
if PATH="${container_fake_bin}:${PATH}" \
  FAKE_DOCKER_LOG="${fake_docker_log}" \
  FAKE_MANAGED=false \
  ROBOT_CONTROL_CONTAINER=foreign-service \
  "${container_fixture}/scripts/build/setup_cross_container.sh" \
  >"${temp}/container-unmanaged.log" 2>&1; then
  echo "Container setup unexpectedly accepted an unmanaged name collision" >&2
  exit 1
fi
grep -q 'Refusing to replace unmanaged container' \
  "${temp}/container-unmanaged.log"
test ! -e "${fake_docker_log}"

container_repository_sha256="$(
  printf '%s' "${container_fixture}" | sha256sum | cut -d' ' -f1
)"
container_clone_id="$(
  printf '%s' "${container_fixture}" | sha256sum | cut -c1-12
)"
if PATH="${container_fake_bin}:${PATH}" \
  FAKE_DOCKER_LOG="${fake_docker_log}" \
  FAKE_MANAGED=true \
  FAKE_REPOSITORY="${container_repository_sha256}" \
  FAKE_CLONE_ID="${container_clone_id}" \
  FAKE_OWNER_UID=99999 \
  FAKE_OWNER_GID=99999 \
  FAKE_CONTRACT=foreign-owner \
  ROBOT_CONTROL_CONTAINER=foreign-owner \
  "${container_fixture}/scripts/build/setup_cross_container.sh" \
  >"${temp}/container-foreign-owner.log" 2>&1; then
  echo "Container setup unexpectedly replaced a foreign-owned container" >&2
  exit 1
fi
grep -q 'Refusing to replace container owned by another user' \
  "${temp}/container-foreign-owner.log"
test ! -e "${fake_docker_log}"

# shellcheck disable=SC2016
grep -q 'mktemp "${lock_file}.staging.' \
  "${repo_root}/scripts/build/build_cross_image.sh"
# shellcheck disable=SC2016
grep -q 'mv -- "${lock_staging}" "${lock_file}"' \
  "${repo_root}/scripts/build/build_cross_image.sh"

readonly image_fixture="${temp}/image-fixture"
readonly image_fake_bin="${temp}/image-fake-bin"
readonly image_docker_log="${temp}/image-docker.log"
mkdir -p \
  "${image_fixture}/scripts/build" \
  "${image_fixture}/docker/cross" \
  "${image_fake_bin}"
cp "${repo_root}/scripts/build/build_cross_image.sh" \
  "${image_fixture}/scripts/build/build_cross_image.sh"
cat >"${image_fixture}/scripts/build/verify_cross_image.sh" <<'EOF'
#!/usr/bin/env bash
exit 91
EOF
chmod +x \
  "${image_fixture}/scripts/build/build_cross_image.sh" \
  "${image_fixture}/scripts/build/verify_cross_image.sh"
cat >"${image_fixture}/docker/cross/Dockerfile" <<'EOF'
# syntax=docker/dockerfile:1@sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
FROM scratch
EOF
printf 'fixture package lock\n' \
  >"${image_fixture}/docker/cross/packages.lock"
cat >"${image_fixture}/docker/cross/image.lock" <<'EOF'
schema_version=2
image_name=rk3588-cross
image_tag=stable
image_id=sha256:old
container_name=rk3588-dev
base_image=ubuntu:22.04@sha256:fixture
ubuntu_snapshot=20260814T000000Z
EOF
cp "${image_fixture}/docker/cross/image.lock" \
  "${temp}/image.lock.before"
cat >"${image_fake_bin}/docker" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

printf '%s\n' "$*" >>"${FAKE_DOCKER_LOG}"
if [[ "${1:-}" == "info" ]]; then
  printf 'fixture-daemon\n'
  exit 0
fi
if [[ "${1:-}" == "build" ]]; then
  exit 0
fi
if [[ "${1:-}" == "image" && "${2:-}" == "inspect" ]]; then
  image="${3:-}"
  if [[ "$*" != *"--format"* ]]; then
    if [[ "${image}" == "rk3588-cross:stable" &&
          ! -s "${FAKE_CANONICAL_STATE}" ]]; then
      exit 1
    fi
    exit 0
  fi
  case "$*" in
    *Architecture*) printf 'amd64\n' ;;
    *Id*)
      if [[ "${image}" == "rk3588-cross:stable" ]]; then
        cat "${FAKE_CANONICAL_STATE}"
      else
        printf 'sha256:candidate\n'
      fi
      ;;
    *) printf '\n' ;;
  esac
  exit 0
fi
if [[ "${1:-}" == "run" ]]; then
  cat <<'OBSERVED'
aarch64-linux-gnu
aarch64-linux-gnu-g++ fixture
cmake version fixture
ninja-fixture
GNU ld fixture
aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
OBSERVED
  exit 0
fi
if [[ "${1:-}" == "image" && "${2:-}" == "tag" ]]; then
  source_image="${3:-}"
  target_image="${4:-}"
  if [[ "${target_image}" == "rk3588-cross:stable" ]]; then
    if [[ "${source_image}" == "sha256:old" ||
          "${source_image}" == rk3588-cross:rollback-* ]]; then
      printf 'sha256:old\n' >"${FAKE_CANONICAL_STATE}"
    else
      printf 'sha256:candidate\n' >"${FAKE_CANONICAL_STATE}"
    fi
  fi
  exit 0
fi
if [[ "${1:-}" == "image" && "${2:-}" == "rm" ]]; then
  if [[ "${3:-}" == "rk3588-cross:stable" ]]; then
    : >"${FAKE_CANONICAL_STATE}"
  fi
  exit 0
fi
echo "Unexpected fake docker invocation: $*" >&2
exit 97
EOF
chmod +x "${image_fake_bin}/docker"
readonly image_canonical_state="${temp}/image-canonical.state"
printf 'sha256:old\n' >"${image_canonical_state}"
if PATH="${image_fake_bin}:${PATH}" \
  FAKE_DOCKER_LOG="${image_docker_log}" \
  FAKE_CANONICAL_STATE="${image_canonical_state}" \
  "${image_fixture}/scripts/build/build_cross_image.sh" --update-lock \
  >"${temp}/image-transaction.log" 2>&1; then
  echo "Cross-image build unexpectedly published after verifier failure" >&2
  exit 1
fi
cmp \
  "${temp}/image.lock.before" \
  "${image_fixture}/docker/cross/image.lock"
if grep -q '^image tag .*rk3588-cross:stable$' "${image_docker_log}"; then
  echo "Cross-image build published the canonical tag before verification" >&2
  exit 1
fi
grep -q '^build .*--tag rk3588-cross:candidate-' "${image_docker_log}"

cat >"${image_fixture}/scripts/build/verify_cross_image.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

count_file="$(dirname "$0")/.verify-count"
count=0
if [[ -f "${count_file}" ]]; then
  count="$(cat "${count_file}")"
fi
count=$((count + 1))
printf '%s\n' "${count}" >"${count_file}"
if [[ "${count}" -eq 2 ]]; then
  exit 92
fi
EOF
chmod +x "${image_fixture}/scripts/build/verify_cross_image.sh"
rm -f "${image_fixture}/scripts/build/.verify-count"
: >"${image_docker_log}"
printf 'sha256:old\n' >"${image_canonical_state}"
cp "${temp}/image.lock.before" \
  "${image_fixture}/docker/cross/image.lock"
if PATH="${image_fake_bin}:${PATH}" \
  FAKE_DOCKER_LOG="${image_docker_log}" \
  FAKE_CANONICAL_STATE="${image_canonical_state}" \
  "${image_fixture}/scripts/build/build_cross_image.sh" --update-lock \
  >"${temp}/image-final-verifier-rollback.log" 2>&1; then
  echo "Cross-image build unexpectedly survived final verifier failure" >&2
  exit 1
fi
cmp \
  "${temp}/image.lock.before" \
  "${image_fixture}/docker/cross/image.lock"
grep -Eq \
  '^image tag rk3588-cross:candidate-[^ ]+ rk3588-cross:stable$' \
  "${image_docker_log}"
grep -q '^image tag sha256:old rk3588-cross:stable$' \
  "${image_docker_log}"
python3 - "${image_docker_log}" <<'PY'
import sys

with open(sys.argv[1], encoding="utf-8") as source:
    lines = [line.rstrip("\n") for line in source]
publish = next(
    index
    for index, line in enumerate(lines)
    if line.startswith("image tag rk3588-cross:candidate-")
    and line.endswith(" rk3588-cross:stable")
)
rollback = lines.index("image tag sha256:old rk3588-cross:stable")
assert publish < rollback
PY

readonly image_mv_fail_bin="${temp}/image-mv-fail-bin"
readonly image_mv_fail_state="${temp}/image-mv-fail.state"
mkdir -p "${image_mv_fail_bin}"
cat >"${image_mv_fail_bin}/mv" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

arguments=("$@")
source_path="${arguments[${#arguments[@]}-2]}"
destination="${arguments[${#arguments[@]}-1]}"
if [[ "${source_path}" == "${FAKE_LOCK_FILE}.staging."* &&
      "${destination}" == "${FAKE_LOCK_FILE}" &&
      ! -e "${FAKE_MV_FAIL_STATE}" ]]; then
  : >"${FAKE_MV_FAIL_STATE}"
  exit 93
fi
exec /bin/mv "$@"
EOF
chmod +x "${image_mv_fail_bin}/mv"
cat >"${image_fixture}/scripts/build/verify_cross_image.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
exit 0
EOF
chmod +x "${image_fixture}/scripts/build/verify_cross_image.sh"
: >"${image_docker_log}"
rm -f "${image_mv_fail_state}"
printf 'sha256:old\n' >"${image_canonical_state}"
cp "${temp}/image.lock.before" \
  "${image_fixture}/docker/cross/image.lock"
if PATH="${image_mv_fail_bin}:${image_fake_bin}:${PATH}" \
  FAKE_DOCKER_LOG="${image_docker_log}" \
  FAKE_CANONICAL_STATE="${image_canonical_state}" \
  FAKE_LOCK_FILE="${image_fixture}/docker/cross/image.lock" \
  FAKE_MV_FAIL_STATE="${image_mv_fail_state}" \
  "${image_fixture}/scripts/build/build_cross_image.sh" --update-lock \
  >"${temp}/image-lock-mv-rollback.log" 2>&1; then
  echo "Cross-image build unexpectedly survived lock promotion failure" >&2
  exit 1
fi
test -e "${image_mv_fail_state}"
cmp \
  "${temp}/image.lock.before" \
  "${image_fixture}/docker/cross/image.lock"
test "$(cat "${image_canonical_state}")" = "sha256:old"
grep -Eq \
  '^image tag rk3588-cross:candidate-[^ ]+ rk3588-cross:stable$' \
  "${image_docker_log}"
grep -q '^image tag sha256:old rk3588-cross:stable$' \
  "${image_docker_log}"
grep -Eq \
  '^image rm --force rk3588-cross:candidate-[^ ]+$' \
  "${image_docker_log}"
grep -Eq \
  '^image rm --force rk3588-cross:rollback-[^ ]+$' \
  "${image_docker_log}"
if find "${image_fixture}/docker/cross" -maxdepth 1 -type f \
  \( -name 'image.lock.staging.*' -o -name 'image.lock.previous.*' \) \
  -print -quit | grep -q .; then
  echo "Failed image-lock promotion leaked a staging or backup lock" >&2
  exit 1
fi

cat >"${image_fixture}/scripts/build/verify_cross_image.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

printf '%s\n' "$*" >>"${FAKE_VERIFY_LOG}"
if [[ "${FAKE_BLOCK_CANDIDATE_VERIFY:-}" == "1" &&
      "${1:-}" == rk3588-cross:candidate-* ]]; then
  : >"${FAKE_VERIFY_READY}"
  while [[ ! -e "${FAKE_VERIFY_RELEASE}" ]]; do
    sleep 0.02
  done
fi
EOF
chmod +x "${image_fixture}/scripts/build/verify_cross_image.sh"
rm -f "${image_fixture}/scripts/build/.verify-count"
: >"${image_docker_log}"
printf 'sha256:old\n' >"${image_canonical_state}"
cp "${temp}/image.lock.before" \
  "${image_fixture}/docker/cross/image.lock"
readonly image_verify_ready="${temp}/image-verify.ready"
readonly image_verify_release="${temp}/image-verify.release"
readonly image_verify_log="${temp}/image-verify.log"
fixture_publication_key="$(
  printf '%s\n%s\n' fixture-daemon rk3588-cross:stable |
    sha256sum | awk '{print $1}'
)"
PATH="${image_fake_bin}:${PATH}" \
  FAKE_DOCKER_LOG="${image_docker_log}" \
  FAKE_CANONICAL_STATE="${image_canonical_state}" \
  FAKE_BLOCK_CANDIDATE_VERIFY=1 \
  FAKE_VERIFY_READY="${image_verify_ready}" \
  FAKE_VERIFY_RELEASE="${image_verify_release}" \
  FAKE_VERIFY_LOG="${image_verify_log}" \
  "${image_fixture}/scripts/build/build_cross_image.sh" --update-lock \
  >"${temp}/image-serialized-first.log" 2>&1 &
first_publisher_pid=$!
for _ in $(seq 1 100); do
  if [[ -e "${image_verify_ready}" ]]; then
    break
  fi
  sleep 0.02
done
test -e "${image_verify_ready}"
PATH="${image_fake_bin}:${PATH}" \
  FAKE_DOCKER_LOG="${image_docker_log}" \
  FAKE_CANONICAL_STATE="${image_canonical_state}" \
  FAKE_VERIFY_LOG="${image_verify_log}" \
  ROBOT_CONTROL_CROSS_IMAGE_LOCK_KEY="${fixture_publication_key}" \
  ROBOT_CONTROL_CROSS_IMAGE_LOCK_FD=999999 \
  "${image_fixture}/scripts/build/build_cross_image.sh" --update-lock \
  >"${temp}/image-serialized-second.log" 2>&1 &
second_publisher_pid=$!
sleep 0.2
test "$(grep -c '^build ' "${image_docker_log}")" -eq 1
: >"${image_verify_release}"
wait "${first_publisher_pid}"
wait "${second_publisher_pid}"
test "$(grep -c '^build ' "${image_docker_log}")" -eq 2
test "$(wc -l <"${image_verify_log}")" -eq 4
test "$(cat "${image_canonical_state}")" = "sha256:candidate"
grep -q '^image_id=sha256:candidate$' \
  "${image_fixture}/docker/cross/image.lock"
if find "${image_fixture}/docker/cross" -maxdepth 1 -type f \
  \( -name 'image.lock.staging.*' -o -name 'image.lock.previous.*' \) \
  -print -quit | grep -q .; then
  echo "Serialized image publication leaked a staging or backup lock" >&2
  exit 1
fi

echo "Phase 1 negative-path script tests passed"
