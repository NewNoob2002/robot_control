# Local sysroots

Sysroot trees are ignored by Git. Reviewed identity locks under
`sysroots/locks/` are the only permitted versioned sysroot artifacts.

No actual sysroot is present in this checkout as of 2026-08-14.

`sysroots/` is a convenient local location, not a build requirement. A sysroot
may live at any suitable host path and is selected with
`ROBOT_CONTROL_SYSROOT`.

For example:

```text
sysroots/rk3588-ubuntu2204/
```

or:

```text
$HOME/.cache/robot-control/sysroots/rk3588-ubuntu2204/
```

Collect a sysroot from an authorized release-equivalent RK3588 Ubuntu 22.04
target:

```bash
sysroot_dir="$HOME/.cache/robot-control/sysroots/rk3588-ubuntu2204"
./scripts/sysroot/sync_from_target.sh <ssh-target> "$sysroot_dir"
```

The command writes the tree to `$sysroot_dir` and its external identity lock to
`$sysroot_dir.lock.json`. Synchronization never writes through
`ROBOT_CONTROL_SYSROOT_LOCK`; that variable is reserved for build-time lock
selection. Existing destinations are replaced only when they contain the
robot-control managed-sysroot marker and have a corresponding regular lock
file. A missing destination with an existing adjacent lock is rejected as an
orphaned publication. Caller-supplied lock paths must be regular non-symlink
files. Compliant publishers targeting the same canonical output and adjacent
lock are serialized by a global exclusive lock keyed from those two canonical
paths. The lock is acquired before existing publication state is inspected and
is held through cleanup, so concurrent sync processes cannot publish a sysroot
tree and lock from different transactions.

The sync script:

1. collects `lib`, `usr/lib`, `usr/include`, and target metadata into a staging
   directory;
2. normalizes absolute symlinks;
3. writes relative checksums for target metadata;
4. generates deterministic `.robot-control/sysroot-content.jsonl` records for
   `lib`, `usr/lib`, and `usr/include`;
5. validates metadata, architecture, Ubuntu version, loader, checksums, and the
   current tree contents;
6. writes an external JSON lock containing the content and target-metadata
   identities;
7. replaces only a managed destination after validation succeeds, with
   interruption-aware rollback.

The publication protocol provides process-level serialization and
interruption-aware rollback. It does not claim filesystem power-loss atomicity
for the sysroot tree and its adjacent lock.

Use the validated sysroot for a development/debug cross build:

```bash
ROBOT_CONTROL_SYSROOT="$sysroot_dir" \
ROBOT_CONTROL_PRESET=rk3588-debug \
  ./scripts/build/build_rk3588.sh
```

The build recomputes and validates the sysroot content manifest and requires
the external lock before configuration, then mounts the sysroot read-only at
`/opt/robot-control/sysroot` in an ephemeral `docker run --rm` container. It
does not require the optional persistent development container created by
`scripts/build/setup_cross_container.sh`.

For release use, copy the generated lock to `sysroots/locks/` through normal
review, commit it, keep the worktree copy unchanged, and set
`ROBOT_CONTROL_SYSROOT_LOCK` to that reviewed file. `rk3588-release` rejects
adjacent, untracked, modified, staged, or out-of-tree locks. Sysroot contents
may include target-specific binaries and package metadata. They are build
inputs, not source. Never commit a sysroot.
