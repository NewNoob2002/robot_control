# Sysroot identity locks

This directory may contain reviewed JSON identity locks for externally stored
RK3588 sysroot artifacts. The sysroot trees themselves remain outside Git.

`scripts/sysroot/sync_from_target.sh` writes an adjacent
`<sysroot>.lock.json` file by default. For a release build, copy that small lock
file here through the normal review process and set:

```bash
export ROBOT_CONTROL_SYSROOT=/absolute/path/to/rk3588-ubuntu2204
export ROBOT_CONTROL_SYSROOT_LOCK="$PWD/sysroots/locks/<reviewed-lock>.json"
```

`rk3588-release` accepts only a regular `.json` file in this directory that is
tracked by Git and has no staged or unstaged changes. This makes review state a
separate gate from content-integrity validation. `rk3588-debug` may continue to
use the generated adjacent lock. Explicit lock paths are rejected before path
canonicalization when they are symlinks, so a symlink cannot redirect either
debug or release lock authority.

The lock contains only the artifact identifier, architecture/OS contract, and
content/target-metadata digests. Do not add target addresses, credentials,
package inventories, or sysroot contents. The sync script never writes into
this directory and rejects `ROBOT_CONTROL_SYSROOT_LOCK` as a lock-output
override.
