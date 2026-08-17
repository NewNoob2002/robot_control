#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <ssh-target> <output-sysroot>" >&2
  exit 2
fi

readonly target="$1"
requested_output="$2"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(realpath -- "${script_dir}/../..")"
readonly script_dir repo_root

if [[ -v ROBOT_CONTROL_SYSROOT_LOCK ]]; then
  echo "ROBOT_CONTROL_SYSROOT_LOCK is build-only; sysroot sync always writes the adjacent lock" >&2
  exit 3
fi

if [[ -v ROBOT_CONTROL_SYSROOT_ALLOWED_ROOTS ]]; then
  allowed_roots_value="${ROBOT_CONTROL_SYSROOT_ALLOWED_ROOTS}"
else
  allowed_roots_value="${repo_root}/sysroots:${XDG_CACHE_HOME:-${HOME}/.cache}/robot-control/sysroots"
fi
readonly allowed_roots_value

if [[ -z "${allowed_roots_value}" ]]; then
  echo "No approved sysroot destination roots are configured" >&2
  exit 3
fi

output="$(realpath --canonicalize-missing -- "${requested_output}")"
readonly output

if [[ "${output}" == "/" ]]; then
  echo "Refusing to use / as a sysroot destination" >&2
  exit 3
fi

destination_allowed=false
IFS=: read -r -a allowed_roots <<<"${allowed_roots_value}"
for requested_root in "${allowed_roots[@]}"; do
  if [[ -z "${requested_root}" ]]; then
    echo "Approved sysroot destination roots must not contain empty entries" >&2
    exit 3
  fi
  allowed_root="$(realpath --canonicalize-missing -- "${requested_root}")"
  if [[ "${allowed_root}" == "/" ]]; then
    echo "Refusing to approve / as a sysroot destination root" >&2
    exit 3
  fi
  if [[ "${output}" == "${allowed_root}/"* ]]; then
    destination_allowed=true
    break
  fi
done
readonly destination_allowed

if [[ "${destination_allowed}" != true ]]; then
  echo "Refusing sysroot destination outside approved roots: ${output}" >&2
  exit 3
fi

output_parent="$(dirname "${output}")"
output_name="$(basename "${output}")"
requested_lock_output="${output}.lock.json"
if [[ -L "${requested_lock_output}" ||
      ( -e "${requested_lock_output}" && ! -f "${requested_lock_output}" ) ]]; then
  echo "Refusing adjacent sysroot lock that is not a regular non-symlink file: ${requested_lock_output}" >&2
  exit 3
fi
lock_output="${requested_lock_output}"
lock_parent="$(dirname "${lock_output}")"
lock_name="$(basename "${lock_output}")"
readonly output_parent output_name requested_lock_output
readonly lock_output lock_parent lock_name

reviewed_lock_root="$(
  realpath --canonicalize-missing -- "${repo_root}/sysroots/locks"
)"
readonly reviewed_lock_root
case "${output}" in
  "${reviewed_lock_root}" | "${reviewed_lock_root}/"*)
    echo "Refusing to synchronize a sysroot inside reviewed-lock storage: ${output}" >&2
    exit 3
    ;;
esac
case "${lock_output}" in
  "${reviewed_lock_root}" | "${reviewed_lock_root}/"*)
    echo "Refusing to write a generated lock inside reviewed-lock storage: ${lock_output}" >&2
    exit 3
    ;;
esac

case "${lock_output}" in
  "${output}" | "${output}/"*)
    echo "Refusing to store the sysroot lock inside the sysroot" >&2
    exit 3
    ;;
esac

case "${lock_output}" in
  "${repo_root}/"*)
    lock_repo_path="${lock_output#"${repo_root}/"}"
    if git -C "${repo_root}" ls-files --error-unmatch -- \
      "${lock_repo_path}" >/dev/null 2>&1; then
      echo "Refusing to overwrite a Git-tracked sysroot lock: ${lock_repo_path}" >&2
      exit 3
    fi
    ;;
esac

publication_key="$(
  printf '%s\n%s\n' "${output}" "${lock_output}" |
    sha256sum | awk '{print $1}'
)"
readonly publication_key
readonly publication_lock="/tmp/robot-control-sysroot-${publication_key}.lock"
publication_lock_held=false
if [[ "${ROBOT_CONTROL_SYSROOT_PUBLICATION_LOCK_KEY:-}" == "${publication_key}" &&
      "${ROBOT_CONTROL_SYSROOT_PUBLICATION_LOCK_FD:-}" =~ ^[0-9]+$ ]]; then
  if python3 - \
    "${ROBOT_CONTROL_SYSROOT_PUBLICATION_LOCK_FD}" \
    "${publication_lock}" <<'PY'
import fcntl
import os
import stat
import sys

lock_fd = int(sys.argv[1])
lock_path = sys.argv[2]
fd_stat = os.fstat(lock_fd)
path_stat = os.stat(lock_path, follow_symlinks=False)
if (
    not stat.S_ISREG(fd_stat.st_mode)
    or fd_stat.st_nlink != 1
    or (fd_stat.st_dev, fd_stat.st_ino) != (path_stat.st_dev, path_stat.st_ino)
):
    raise SystemExit(1)
fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
PY
  then
    publication_lock_held=true
  fi
fi
if [[ "${publication_lock_held}" != true ]]; then
  exec python3 - \
    "${publication_lock}" \
    "${publication_key}" \
    "$0" \
    "${target}" \
    "${requested_output}" <<'PY'
import fcntl
import os
import stat
import sys

lock_path, lock_key, script, target, requested_output = sys.argv[1:]
flags = os.O_RDWR | os.O_CREAT
if hasattr(os, "O_NOFOLLOW"):
    flags |= os.O_NOFOLLOW
lock_fd = os.open(lock_path, flags, 0o666)
lock_stat = os.fstat(lock_fd)
if not stat.S_ISREG(lock_stat.st_mode) or lock_stat.st_nlink != 1:
    raise SystemExit(f"Unsafe sysroot publication lock: {lock_path}")
try:
    os.fchmod(lock_fd, 0o666)
except PermissionError:
    pass
fcntl.flock(lock_fd, fcntl.LOCK_EX)
os.set_inheritable(lock_fd, True)
environment = os.environ.copy()
environment["ROBOT_CONTROL_SYSROOT_PUBLICATION_LOCK_KEY"] = lock_key
environment["ROBOT_CONTROL_SYSROOT_PUBLICATION_LOCK_FD"] = str(lock_fd)
os.execvpe(
    "bash",
    ["bash", script, target, requested_output],
    environment,
)
PY
fi

if [[ -e "${output}" || -L "${output}" ]]; then
  managed_marker=""
  if [[ -f "${output}/.robot-control/managed-sysroot" ]]; then
    managed_marker="$(<"${output}/.robot-control/managed-sysroot")"
  fi
  if [[ ! -d "${output}" || -L "${output}" ||
    "${managed_marker}" != "robot-control-sysroot-v1" ]]; then
    echo "Refusing to replace an unmanaged sysroot destination: ${output}" >&2
    exit 3
  fi
  [[ -f "${lock_output}" && ! -L "${lock_output}" ]] || {
    echo "Refusing to replace a managed sysroot without its external lock: ${lock_output}" >&2
    exit 3
  }
  "${script_dir}/validate_sysroot.sh" "${output}" "${lock_output}" >/dev/null || {
    echo "Refusing to replace a managed sysroot whose external lock does not validate: ${output}" >&2
    exit 3
  }
elif [[ -e "${lock_output}" || -L "${lock_output}" ]]; then
  echo "Refusing to replace an orphaned sysroot lock: ${lock_output}" >&2
  exit 3
fi

staging=""
staging_identity=""
backup=""
lock_staging=""
lock_staging_identity=""
lock_backup=""
tmp_manifest=""
promotion_committed=false

cleanup() {
  local output_identity=""
  local lock_identity=""

  if [[ "${promotion_committed:-false}" != true ]]; then
    if [[ -d "${output}" && ! -L "${output}" ]]; then
      output_identity="$(stat -c '%d:%i' "${output}")"
      if [[ "${output_identity}" == "${staging_identity}" ]]; then
        rm -rf -- "${output}"
      fi
    fi
    if [[ -n "${backup:-}" && ! -e "${output}" && ! -L "${output}" ]]; then
      if mv -- "${backup}" "${output}"; then
        backup=""
      else
        echo "Failed to restore previous sysroot; preserved at ${backup}" >&2
      fi
    fi

    if [[ -n "${lock_staging_identity:-}" &&
      -f "${lock_output}" && ! -L "${lock_output}" ]]; then
      lock_identity="$(stat -c '%d:%i' "${lock_output}")"
      if [[ "${lock_identity}" == "${lock_staging_identity}" ]]; then
        rm -f -- "${lock_output}"
      fi
    fi
    if [[ -n "${lock_backup:-}" && ! -e "${lock_output}" && ! -L "${lock_output}" ]]; then
      if mv -- "${lock_backup}" "${lock_output}"; then
        lock_backup=""
      else
        echo "Failed to restore previous sysroot lock; preserved at ${lock_backup}" >&2
      fi
    fi
  fi

  if [[ -n "${staging:-}" ]]; then
    rm -rf -- "${staging}"
  fi
  if [[ -n "${lock_staging:-}" ]]; then
    rm -f -- "${lock_staging}"
  fi
  if [[ -n "${tmp_manifest:-}" ]]; then
    rm -rf -- "${tmp_manifest}"
  fi
  if [[ "${promotion_committed:-false}" == true ]]; then
    [[ -z "${backup:-}" ]] || rm -rf -- "${backup}"
    [[ -z "${lock_backup:-}" ]] || rm -f -- "${lock_backup}"
  fi
}

mkdir -p -- "${output_parent}" "${lock_parent}"
staging="$(mktemp -d "${output_parent}/.${output_name}.staging.XXXXXX")"
staging_identity="$(stat -c '%d:%i' "${staging}")"
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
lock_staging="$(mktemp "${lock_parent}/.${lock_name}.staging.XXXXXX")"
lock_staging_identity="$(stat -c '%d:%i' "${lock_staging}")"
readonly staging_identity lock_staging_identity

mkdir -p "${staging}/.robot-control"
printf 'robot-control-sysroot-v1\n' \
  >"${staging}/.robot-control/managed-sysroot"

if ssh "${target}" 'command -v rsync >/dev/null 2>&1'; then
  for path in lib usr/include usr/lib; do
    rsync --archive --links --numeric-ids \
      "${target}:/${path}/" "${staging}/${path}/"
  done
else
  ssh "${target}" \
    'tar --create --file=- --numeric-owner --one-file-system --directory=/ lib usr/include usr/lib' \
    | tar --extract --file=- --no-same-owner --directory="${staging}"
fi

# Collect the manifest separately without assuming privileged target writes.
tmp_manifest="$(mktemp -d)"
ssh "${target}" \
  'tmp=$(mktemp -d); trap '\''rm -rf "$tmp"'\'' EXIT; bash -s "$tmp"; tar -C "$tmp" -cf - .' \
  <"${script_dir}/collect_target_manifest.sh" \
  | tar -C "${tmp_manifest}" -xf -
cp -a "${tmp_manifest}/." "${staging}/.robot-control/"
rm -rf -- "${tmp_manifest}"
tmp_manifest=""

"${script_dir}/normalize_sysroot.sh" "${staging}"
"${script_dir}/generate_content_manifest.py" "${staging}"
"${script_dir}/validate_sysroot.sh" "${staging}"
"${script_dir}/write_sysroot_lock.py" \
  "${staging}" \
  "${lock_staging}" \
  --artifact-id "${output_name}" >/dev/null

if [[ -e "${output}" || -L "${output}" ]]; then
  backup="$(mktemp -d "${output_parent}/.${output_name}.previous.XXXXXX")"
  rmdir -- "${backup}"
  mv -- "${output}" "${backup}"
fi
if [[ -e "${lock_output}" || -L "${lock_output}" ]]; then
  lock_backup="$(mktemp "${lock_parent}/.${lock_name}.previous.XXXXXX")"
  rm -f -- "${lock_backup}"
  mv -- "${lock_output}" "${lock_backup}"
fi
mv -- "${staging}" "${output}"
staging=""
mv -- "${lock_staging}" "${lock_output}"
lock_staging=""
"${script_dir}/validate_sysroot.sh" "${output}" "${lock_output}" >/dev/null
promotion_committed=true

printf 'sysroot=%s\nlock=%s\n' "${output}" "${lock_output}"
