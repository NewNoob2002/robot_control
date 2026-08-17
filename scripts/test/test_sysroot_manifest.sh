#!/usr/bin/env bash
set -euo pipefail

# Keep fixture permissions deterministic regardless of the invoking shell.
umask 0022

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly repo_root
temp="$(mktemp -d)"
readonly temp
trap 'rm -rf "${temp}"' EXIT

readonly sysroot="${temp}/fixture"
readonly metadata="${sysroot}/.robot-control"
mkdir -p \
  "${sysroot}/lib/aarch64-linux-gnu" \
  "${sysroot}/usr/include/robot-control" \
  "${sysroot}/usr/lib/aarch64-linux-gnu" \
  "${metadata}"

python3 - "${sysroot}/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1" <<'PY'
import struct
import sys

path = sys.argv[1]
ident = b"\x7fELF" + bytes([2, 1, 1, 0, 0]) + bytes(7)
header = struct.pack("<HHIQQQIHHHHHH", 3, 183, 1, 0, 0, 0, 0, 64, 0, 0, 0, 0, 0)
with open(path, "wb") as output:
    output.write(ident + header)
PY
chmod 0755 "${sysroot}/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1"

printf '#define ROBOT_CONTROL_FIXTURE 1\n' \
  >"${sysroot}/usr/include/robot-control/fixture.h"
printf 'fixture library\n' \
  >"${sysroot}/usr/lib/aarch64-linux-gnu/libfixture.so.1"
chmod 0644 \
  "${sysroot}/usr/include/robot-control/fixture.h" \
  "${sysroot}/usr/lib/aarch64-linux-gnu/libfixture.so.1"
ln -s /usr/lib/aarch64-linux-gnu/libfixture.so.1 \
  "${sysroot}/usr/lib/libfixture.so"
ln -s /usr/include/robot-control/fixture.h \
  "${sysroot}/usr/include/robot-control/fixture-alias.h"

cat >"${metadata}/os-release" <<'EOF'
ID=ubuntu
VERSION_ID="22.04"
PRETTY_NAME="Ubuntu 22.04 fixture"
EOF
printf 'arm64\n' >"${metadata}/architecture.txt"
printf 'glibc 2.35\n' >"${metadata}/glibc-version.txt"
printf 'ldd fixture\n' >"${metadata}/ldd-version.txt"
printf 'fixture\t1.0\tarm64\n' >"${metadata}/packages.tsv"
printf '/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1\n' \
  >"${metadata}/sysroot-files.txt"
printf 'Linux fixture aarch64\n' >"${metadata}/uname.txt"
(
  cd "${metadata}"
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

"${repo_root}/scripts/sysroot/normalize_sysroot.sh" "${sysroot}"
"${repo_root}/scripts/sysroot/generate_content_manifest.py" "${sysroot}" >/dev/null
"${repo_root}/scripts/sysroot/write_sysroot_lock.py" \
  "${sysroot}" "${temp}/fixture.lock.json" >/dev/null
"${repo_root}/scripts/sysroot/validate_sysroot.sh" "${sysroot}" \
  >"${temp}/valid.log"
"${repo_root}/scripts/sysroot/validate_sysroot.sh" \
  "${sysroot}" "${temp}/fixture.lock.json" >"${temp}/locked-valid.log"
ln -s "${temp}/fixture.lock.json" "${temp}/fixture-lock-symlink.json"
if "${repo_root}/scripts/sysroot/validate_sysroot.sh" \
  "${sysroot}" "${temp}/fixture-lock-symlink.json" \
  >"${temp}/symlink-lock.log" 2>&1; then
  echo "Validator unexpectedly accepted a symlink external lock" >&2
  exit 1
fi
grep -q 'regular non-symlink file' "${temp}/symlink-lock.log"
cp "${metadata}/sysroot-content.jsonl" "${temp}/first-content.jsonl"
cp "${metadata}/sysroot-content.sha256" "${temp}/first-content.sha256"
"${repo_root}/scripts/sysroot/generate_content_manifest.py" "${sysroot}" >/dev/null
cmp "${temp}/first-content.jsonl" "${metadata}/sysroot-content.jsonl"
cmp "${temp}/first-content.sha256" "${metadata}/sysroot-content.sha256"

grep -q '^arm64$' "${metadata}/architecture.txt"
grep -Eq '^[0-9a-f]{64}  architecture\.txt$' "${metadata}/manifest.sha256"
grep -Eq '^[0-9a-f]{64}  sysroot-content\.jsonl$' \
  "${metadata}/sysroot-content.sha256"
grep -q '"schema_version": 1' "${temp}/fixture.lock.json"
if grep -q "${temp}" "${metadata}/manifest.sha256" \
  "${metadata}/sysroot-content.sha256"; then
  echo "Checksums unexpectedly contain an absolute fixture path" >&2
  exit 1
fi

python3 - "${metadata}/sysroot-content.jsonl" <<'PY'
import json
import sys

records = {}
with open(sys.argv[1], encoding="utf-8") as source:
    for line in source:
        record = json.loads(line)
        records[record["relative_path"]] = record

header = records["usr/include/robot-control/fixture.h"]
assert header["type"] == "file"
assert header["mode"] == "0644"
assert len(header["sha256"]) == 64
assert header["symlink_target"] is None

link = records["usr/lib/libfixture.so"]
assert link["type"] == "symlink"
assert link["mode"] == "0777"
assert link["symlink_target"] == "aarch64-linux-gnu/libfixture.so.1"
assert link["sha256"] is None

header_link = records["usr/include/robot-control/fixture-alias.h"]
assert header_link["type"] == "symlink"
assert header_link["symlink_target"] == "fixture.h"

directory = records["usr/include"]
assert directory["type"] == "directory"
assert directory["sha256"] is None
PY

cp -a "${sysroot}" "${temp}/mutated-tree"
printf 'tampered\n' >>"${temp}/mutated-tree/usr/include/robot-control/fixture.h"
if "${repo_root}/scripts/sysroot/validate_sysroot.sh" "${temp}/mutated-tree" \
  >"${temp}/mutated-tree.log" 2>&1; then
  echo "Validator unexpectedly accepted mutated sysroot content" >&2
  exit 1
fi
grep -q 'current tree does not match content manifest' "${temp}/mutated-tree.log"

cp -a "${sysroot}" "${temp}/bad-metadata"
printf 'modified\n' >>"${temp}/bad-metadata/.robot-control/packages.tsv"
if "${repo_root}/scripts/sysroot/validate_sysroot.sh" "${temp}/bad-metadata" \
  >"${temp}/bad-metadata.log" 2>&1; then
  echo "Validator unexpectedly accepted modified target metadata" >&2
  exit 1
fi
grep -q 'target metadata checksum verification failed' \
  "${temp}/bad-metadata.log"

cp -a "${sysroot}" "${temp}/absolute-metadata-names"
readonly absolute_metadata="${temp}/absolute-metadata-names/.robot-control"
sha256sum \
  "${absolute_metadata}/architecture.txt" \
  "${absolute_metadata}/glibc-version.txt" \
  "${absolute_metadata}/ldd-version.txt" \
  "${absolute_metadata}/os-release" \
  "${absolute_metadata}/packages.tsv" \
  "${absolute_metadata}/sysroot-files.txt" \
  "${absolute_metadata}/uname.txt" \
  >"${absolute_metadata}/manifest.sha256"
if "${repo_root}/scripts/sysroot/validate_sysroot.sh" \
  "${temp}/absolute-metadata-names" \
  >"${temp}/absolute-metadata-names.log" 2>&1; then
  echo "Validator unexpectedly accepted absolute target metadata names" >&2
  exit 1
fi
grep -q 'target metadata checksum contains a non-relative name' \
  "${temp}/absolute-metadata-names.log"

cp -a "${sysroot}" "${temp}/wrong-architecture"
printf 'amd64\n' >"${temp}/wrong-architecture/.robot-control/architecture.txt"
(
  cd "${temp}/wrong-architecture/.robot-control"
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
if "${repo_root}/scripts/sysroot/validate_sysroot.sh" \
  "${temp}/wrong-architecture" >"${temp}/wrong-architecture.log" 2>&1; then
  echo "Validator unexpectedly accepted non-arm64 metadata" >&2
  exit 1
fi
grep -q 'target architecture is not arm64' "${temp}/wrong-architecture.log"

cp -a "${sysroot}" "${temp}/wrong-os"
sed -i 's/VERSION_ID=\"22.04\"/VERSION_ID=\"24.04\"/' \
  "${temp}/wrong-os/.robot-control/os-release"
(
  cd "${temp}/wrong-os/.robot-control"
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
if "${repo_root}/scripts/sysroot/validate_sysroot.sh" "${temp}/wrong-os" \
  >"${temp}/wrong-os.log" 2>&1; then
  echo "Validator unexpectedly accepted non-Ubuntu-22.04 metadata" >&2
  exit 1
fi
grep -q 'Sysroot is not Ubuntu 22.04' "${temp}/wrong-os.log"

cp -a "${sysroot}" "${temp}/bad-content-checksum"
printf '0%.0s' {1..64} \
  >"${temp}/bad-content-checksum/.robot-control/sysroot-content.sha256"
printf '  sysroot-content.jsonl\n' \
  >>"${temp}/bad-content-checksum/.robot-control/sysroot-content.sha256"
if "${repo_root}/scripts/sysroot/validate_sysroot.sh" \
  "${temp}/bad-content-checksum" >"${temp}/bad-content-checksum.log" 2>&1; then
  echo "Validator unexpectedly accepted a bad content manifest checksum" >&2
  exit 1
fi
grep -q 'content manifest checksum verification failed' \
  "${temp}/bad-content-checksum.log"

cp -a "${sysroot}" "${temp}/relocked-mutated-tree"
printf 'tampered and relocked\n' \
  >>"${temp}/relocked-mutated-tree/usr/include/robot-control/fixture.h"
"${repo_root}/scripts/sysroot/generate_content_manifest.py" \
  "${temp}/relocked-mutated-tree" >/dev/null
"${repo_root}/scripts/sysroot/validate_sysroot.sh" \
  "${temp}/relocked-mutated-tree" >/dev/null
if "${repo_root}/scripts/sysroot/validate_sysroot.sh" \
  "${temp}/relocked-mutated-tree" "${temp}/fixture.lock.json" \
  >"${temp}/relocked-mutated-tree.log" 2>&1; then
  echo "External lock unexpectedly accepted a locally regenerated manifest" >&2
  exit 1
fi
grep -q 'external identity verification failed' \
  "${temp}/relocked-mutated-tree.log"

if "${repo_root}/scripts/sysroot/write_sysroot_lock.py" \
  "${sysroot}" "${metadata}/unsafe-external-lock.json" \
  >"${temp}/inside-lock.log" 2>&1; then
  echo "Lock writer unexpectedly stored a trust anchor inside the sysroot" >&2
  exit 1
fi
grep -q 'must be stored outside the sysroot' "${temp}/inside-lock.log"

readonly writer_redirect_target="${temp}/writer-redirect-target.json"
readonly writer_symlink_output="${temp}/writer-symlink-output.json"
cp "${temp}/fixture.lock.json" "${writer_redirect_target}"
cp "${writer_redirect_target}" "${temp}/writer-redirect-target.before"
ln -s "${writer_redirect_target}" "${writer_symlink_output}"
if "${repo_root}/scripts/sysroot/write_sysroot_lock.py" \
  "${sysroot}" "${writer_symlink_output}" \
  >"${temp}/writer-symlink-output.log" 2>&1; then
  echo "Lock writer unexpectedly followed a caller-supplied output symlink" >&2
  exit 1
fi
grep -q 'regular non-symlink file' "${temp}/writer-symlink-output.log"
cmp "${temp}/writer-redirect-target.before" "${writer_redirect_target}"
if find "${temp}" -maxdepth 1 \
  -name '.writer-symlink-output.json.*' \
  -print -quit | grep -q .; then
  echo "Rejected lock-writer symlink leaked a temporary file" >&2
  exit 1
fi

assert_unsafe_symlink_rejected() {
  local name="$1"
  local expected_message="$2"
  local fixture="${temp}/${name}"

  if "${repo_root}/scripts/sysroot/generate_content_manifest.py" "${fixture}" \
    >"${temp}/${name}-generate.log" 2>&1; then
    echo "Manifest generator unexpectedly accepted ${name}" >&2
    exit 1
  fi
  grep -q "${expected_message}" "${temp}/${name}-generate.log"

  if "${repo_root}/scripts/sysroot/validate_sysroot.sh" "${fixture}" \
    >"${temp}/${name}-validate.log" 2>&1; then
    echo "Validator unexpectedly accepted ${name}" >&2
    exit 1
  fi
  grep -q 'content tree contains an unsafe symlink' \
    "${temp}/${name}-validate.log"
}

cp -a "${sysroot}" "${temp}/absolute-symlink"
ln -s /tmp/outside \
  "${temp}/absolute-symlink/usr/lib/unsafe-link"
assert_unsafe_symlink_rejected absolute-symlink \
  'unsafe absolute symlink target'

cp -a "${sysroot}" "${temp}/relative-escape-symlink"
ln -s ../../../outside \
  "${temp}/relative-escape-symlink/usr/lib/unsafe-link"
assert_unsafe_symlink_rejected relative-escape-symlink \
  'unsafe symlink target escapes sysroot'

cp -a "${sysroot}" "${temp}/multi-hop-escape-symlink"
ln -s ../outside \
  "${temp}/multi-hop-escape-symlink/escape-link"
ln -s ../../escape-link \
  "${temp}/multi-hop-escape-symlink/usr/lib/unsafe-link"
assert_unsafe_symlink_rejected multi-hop-escape-symlink \
  'unsafe symlink target escapes sysroot'

cp -a "${sysroot}" "${temp}/cyclic-symlink"
ln -s cycle-b "${temp}/cyclic-symlink/cycle-a"
ln -s cycle-a "${temp}/cyclic-symlink/cycle-b"
ln -s ../../cycle-a \
  "${temp}/cyclic-symlink/usr/lib/unsafe-link"
assert_unsafe_symlink_rejected cyclic-symlink \
  'cyclic symlink target'

python3 - "${repo_root}/scripts/sysroot/sync_from_target.sh" <<'PY'
import sys

script = open(sys.argv[1], encoding="utf-8").read()
normalize = script.index('"${script_dir}/normalize_sysroot.sh" "${staging}"')
generate = script.index(
    '"${script_dir}/generate_content_manifest.py" "${staging}"'
)
validate = script.index('"${script_dir}/validate_sysroot.sh" "${staging}"')
assert normalize < generate < validate
PY

readonly fake_bin="${temp}/fake-bin"
readonly fake_target="${temp}/fake-target"
readonly fake_metadata="${temp}/fake-metadata"
mkdir -p "${fake_bin}" "${fake_target}" "${fake_metadata}"
cp -a "${sysroot}/lib" "${fake_target}/"
mkdir -p "${fake_target}/usr"
cp -a "${sysroot}/usr/include" "${fake_target}/usr/"
cp -a "${sysroot}/usr/lib" "${fake_target}/usr/"
cp -a \
  "${metadata}/architecture.txt" \
  "${metadata}/glibc-version.txt" \
  "${metadata}/ldd-version.txt" \
  "${metadata}/manifest.sha256" \
  "${metadata}/os-release" \
  "${metadata}/packages.tsv" \
  "${metadata}/sysroot-files.txt" \
  "${metadata}/uname.txt" \
  "${fake_metadata}/"

cat >"${fake_bin}/ssh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${TEST_SSH_MARKER:-}" ]]; then
  : >"${TEST_SSH_MARKER}"
fi
shift
if [[ "$*" == *"command -v rsync"* ]]; then
  if [[ "${TEST_SSH_BLOCK:-}" == "1" ]]; then
    : >"${TEST_SSH_READY}"
    while [[ ! -e "${TEST_SSH_RELEASE}" ]]; do
      sleep 0.02
    done
  fi
  exit 0
fi
tar -C "${TEST_TARGET_METADATA}" -cf - .
EOF

cat >"${fake_bin}/rsync" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

source_path="${@: -2:1}"
destination="${@: -1}"
remote_path="${source_path#*:}"
mkdir -p "${destination}"
cp -a "${TEST_TARGET_ROOT}${remote_path}/." "${destination}/"
EOF
chmod +x "${fake_bin}/ssh" "${fake_bin}/rsync"

readonly approved_root="${temp}/approved"
readonly ssh_marker="${temp}/ssh-invoked"
if PATH="${fake_bin}:${PATH}" \
  TEST_SSH_MARKER="${ssh_marker}" \
  ROBOT_CONTROL_SYSROOT_ALLOWED_ROOTS="${approved_root}" \
  "${repo_root}/scripts/sysroot/sync_from_target.sh" \
  fixture-target "${temp}/not-approved/sysroot" \
  >"${temp}/disallowed-sync.log" 2>&1; then
  echo "Sync unexpectedly accepted a destination outside approved roots" >&2
  exit 1
fi
grep -q 'outside approved roots' "${temp}/disallowed-sync.log"
if [[ -e "${ssh_marker}" ]]; then
  echo "Sync contacted the target before rejecting its destination" >&2
  exit 1
fi

PATH="${fake_bin}:${PATH}" \
TEST_TARGET_METADATA="${fake_metadata}" \
TEST_TARGET_ROOT="${fake_target}" \
ROBOT_CONTROL_SYSROOT_ALLOWED_ROOTS="${temp}/unused-approved:${approved_root}" \
  "${repo_root}/scripts/sysroot/sync_from_target.sh" \
  fixture-target "${approved_root}/successful" \
  >"${temp}/successful-sync.log"
"${repo_root}/scripts/sysroot/validate_sysroot.sh" \
  "${approved_root}/successful" \
  "${approved_root}/successful.lock.json" >/dev/null
grep -q '^robot-control-sysroot-v1$' \
  "${approved_root}/successful/.robot-control/managed-sysroot"
test -f "${approved_root}/successful.lock.json"

readonly adjacent_symlink_output="${approved_root}/adjacent-symlink"
readonly adjacent_redirect_target="${temp}/adjacent-redirect-target.json"
cp -a "${approved_root}/successful" "${adjacent_symlink_output}"
"${repo_root}/scripts/sysroot/write_sysroot_lock.py" \
  "${adjacent_symlink_output}" \
  "${adjacent_redirect_target}" \
  --artifact-id adjacent-symlink >/dev/null
cp "${adjacent_redirect_target}" "${temp}/adjacent-redirect-target.before"
ln -s "${adjacent_redirect_target}" "${adjacent_symlink_output}.lock.json"
rm -f "${ssh_marker}"
if PATH="${fake_bin}:${PATH}" \
  TEST_SSH_MARKER="${ssh_marker}" \
  ROBOT_CONTROL_SYSROOT_ALLOWED_ROOTS="${approved_root}" \
  "${repo_root}/scripts/sysroot/sync_from_target.sh" \
  fixture-target "${adjacent_symlink_output}" \
  >"${temp}/adjacent-lock-symlink.log" 2>&1; then
  echo "Sync unexpectedly followed an adjacent lock symlink" >&2
  exit 1
fi
grep -q 'adjacent sysroot lock.*regular non-symlink file' \
  "${temp}/adjacent-lock-symlink.log"
cmp "${temp}/adjacent-redirect-target.before" "${adjacent_redirect_target}"
test -L "${adjacent_symlink_output}.lock.json"
if [[ -e "${ssh_marker}" ]]; then
  echo "Sync contacted the target before rejecting an adjacent lock symlink" >&2
  exit 1
fi
if find "${approved_root}" -maxdepth 1 \
  \( -name '.adjacent-symlink.staging.*' \
  -o -name '.adjacent-symlink.previous.*' \
  -o -name '.adjacent-symlink.lock.json.staging.*' \
  -o -name '.adjacent-symlink.lock.json.previous.*' \) \
  -print -quit | grep -q .; then
  echo "Rejected adjacent lock symlink leaked staging or backup state" >&2
  exit 1
fi

readonly serialized_output="${approved_root}/serialized"
readonly serialized_first_marker="${temp}/serialized-first-ssh"
readonly serialized_second_marker="${temp}/serialized-second-ssh"
readonly serialized_ready="${temp}/serialized.ready"
readonly serialized_release="${temp}/serialized.release"
serialized_publication_key="$(
  printf '%s\n%s\n' \
    "${serialized_output}" \
    "${serialized_output}.lock.json" |
    sha256sum | awk '{print $1}'
)"
PATH="${fake_bin}:${PATH}" \
  TEST_SSH_MARKER="${serialized_first_marker}" \
  TEST_SSH_BLOCK=1 \
  TEST_SSH_READY="${serialized_ready}" \
  TEST_SSH_RELEASE="${serialized_release}" \
  TEST_TARGET_METADATA="${fake_metadata}" \
  TEST_TARGET_ROOT="${fake_target}" \
  ROBOT_CONTROL_SYSROOT_ALLOWED_ROOTS="${approved_root}" \
  "${repo_root}/scripts/sysroot/sync_from_target.sh" \
  fixture-target "${serialized_output}" \
  >"${temp}/serialized-first.log" 2>&1 &
serialized_first_pid=$!
for _ in $(seq 1 100); do
  if [[ -e "${serialized_ready}" ]]; then
    break
  fi
  sleep 0.02
done
test -e "${serialized_ready}"
PATH="${fake_bin}:${PATH}" \
  TEST_SSH_MARKER="${serialized_second_marker}" \
  TEST_TARGET_METADATA="${fake_metadata}" \
  TEST_TARGET_ROOT="${fake_target}" \
  ROBOT_CONTROL_SYSROOT_ALLOWED_ROOTS="${approved_root}" \
  ROBOT_CONTROL_SYSROOT_PUBLICATION_LOCK_KEY="${serialized_publication_key}" \
  ROBOT_CONTROL_SYSROOT_PUBLICATION_LOCK_FD=999999 \
  "${repo_root}/scripts/sysroot/sync_from_target.sh" \
  fixture-target "${serialized_output}" \
  >"${temp}/serialized-second.log" 2>&1 &
serialized_second_pid=$!
sleep 0.2
test ! -e "${serialized_second_marker}"
: >"${serialized_release}"
wait "${serialized_first_pid}"
wait "${serialized_second_pid}"
test -e "${serialized_second_marker}"
"${repo_root}/scripts/sysroot/validate_sysroot.sh" \
  "${serialized_output}" \
  "${serialized_output}.lock.json" >/dev/null
if find "${approved_root}" -maxdepth 1 \
  \( -name '.serialized.staging.*' \
  -o -name '.serialized.previous.*' \
  -o -name '.serialized.lock.json.staging.*' \
  -o -name '.serialized.lock.json.previous.*' \) \
  -print -quit | grep -q .; then
  echo "Serialized sysroot publication leaked staging or backup state" >&2
  exit 1
fi

mkdir -p "${approved_root}/unmanaged"
printf 'unmanaged\n' >"${approved_root}/unmanaged/keep"
rm -f "${ssh_marker}"
if PATH="${fake_bin}:${PATH}" \
  TEST_SSH_MARKER="${ssh_marker}" \
  ROBOT_CONTROL_SYSROOT_ALLOWED_ROOTS="${approved_root}" \
  "${repo_root}/scripts/sysroot/sync_from_target.sh" \
  fixture-target "${approved_root}/unmanaged" \
  >"${temp}/unmanaged-sync.log" 2>&1; then
  echo "Sync unexpectedly replaced an unmanaged destination" >&2
  exit 1
fi
grep -q 'unmanaged sysroot destination' "${temp}/unmanaged-sync.log"
test -f "${approved_root}/unmanaged/keep"
if [[ -e "${ssh_marker}" ]]; then
  echo "Sync contacted the target before rejecting an unmanaged destination" >&2
  exit 1
fi

readonly failing_bin="${temp}/failing-bin"
readonly failed_output="${approved_root}/recovery-failure"
readonly sync_tmp="${temp}/sync-tmp"
mkdir -p "${failing_bin}" "${sync_tmp}"
cp -a "${approved_root}/successful" "${failed_output}"
printf 'old sysroot\n' >"${failed_output}/old-marker"
"${repo_root}/scripts/sysroot/generate_content_manifest.py" \
  "${failed_output}"
"${repo_root}/scripts/sysroot/write_sysroot_lock.py" \
  "${failed_output}" "${failed_output}.lock.json" >/dev/null
cat >"${failing_bin}/mv" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

arguments=("$@")
source_path="${arguments[${#arguments[@]}-2]}"
destination="${arguments[${#arguments[@]}-1]}"
if [[ "${destination}" == "${TEST_FAIL_OUTPUT}" ]] \
  && [[ "${source_path}" == *".staging."* \
    || "${source_path}" == *".previous."* ]]; then
  exit 1
fi
exec /bin/mv "$@"
EOF
chmod +x "${failing_bin}/mv"

if PATH="${failing_bin}:${fake_bin}:${PATH}" \
  TMPDIR="${sync_tmp}" \
  TEST_FAIL_OUTPUT="${failed_output}" \
  TEST_TARGET_METADATA="${fake_metadata}" \
  TEST_TARGET_ROOT="${fake_target}" \
  ROBOT_CONTROL_SYSROOT_ALLOWED_ROOTS="${approved_root}" \
  "${repo_root}/scripts/sysroot/sync_from_target.sh" \
  fixture-target "${failed_output}" \
  >"${temp}/failed-promotion.log" 2>&1; then
  echo "Sync unexpectedly succeeded when promotion and recovery failed" >&2
  exit 1
fi
grep -q 'Failed to restore previous sysroot; preserved at' \
  "${temp}/failed-promotion.log"
backup_path="$(
  find "${approved_root}" -maxdepth 1 -type d \
    -name '.recovery-failure.previous.*' -print -quit
)"
if [[ -z "${backup_path}" || ! -f "${backup_path}/old-marker" ]]; then
  echo "Failed promotion did not preserve the previous sysroot backup" >&2
  exit 1
fi
if find "${approved_root}" -maxdepth 1 -type d \
  -name '.recovery-failure.staging.*' -print -quit | grep -q .; then
  echo "Failed promotion leaked a staging directory" >&2
  exit 1
fi
if find "${sync_tmp}" -mindepth 1 -print -quit | grep -q .; then
  echo "Failed promotion leaked temporary metadata" >&2
  exit 1
fi

echo "Sysroot content manifest regression tests passed"
