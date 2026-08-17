#!/usr/bin/env python3
"""Generate a deterministic content manifest for a robot-control sysroot."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import stat
import tempfile
from collections import deque
from pathlib import Path
from typing import Deque, Iterator


MANIFEST_NAME = "sysroot-content.jsonl"
CHECKSUM_NAME = "sysroot-content.sha256"
COVERED_ROOTS = ("lib", "usr/lib", "usr/include")
MAX_SYMLINK_HOPS = 128


def sha256_file(path: Path) -> str:
    """Return the SHA-256 digest of one regular file without following links."""
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def entry_type(mode: int) -> str:
    """Return a stable type name for an lstat mode value."""
    if stat.S_ISREG(mode):
        return "file"
    if stat.S_ISDIR(mode):
        return "directory"
    if stat.S_ISLNK(mode):
        return "symlink"
    if stat.S_ISCHR(mode):
        return "character-device"
    if stat.S_ISBLK(mode):
        return "block-device"
    if stat.S_ISFIFO(mode):
        return "fifo"
    if stat.S_ISSOCK(mode):
        return "socket"
    return "unknown"


def resolve_symlink_within_sysroot(sysroot: Path, link: Path) -> None:
    """Reject a link whose complete target chain escapes or cycles."""
    target = os.readlink(link)
    if os.path.isabs(target):
        raise ValueError(
            f"unsafe absolute symlink target: {link.relative_to(sysroot)} -> {target}"
        )

    resolved_parts = list(link.parent.relative_to(sysroot).parts)
    pending: Deque[str] = deque(Path(target).parts)
    visited_links: set[Path] = set()
    hops = 0

    while pending:
        part = pending.popleft()
        if part in ("", "."):
            continue
        if part == "..":
            if not resolved_parts:
                raise ValueError(
                    "unsafe symlink target escapes sysroot: "
                    f"{link.relative_to(sysroot)} -> {target}"
                )
            resolved_parts.pop()
            continue

        candidate = sysroot.joinpath(*resolved_parts, part)
        try:
            metadata = candidate.lstat()
        except FileNotFoundError:
            resolved_parts.append(part)
            continue

        if not stat.S_ISLNK(metadata.st_mode):
            resolved_parts.append(part)
            continue

        if candidate in visited_links:
            raise ValueError(
                f"cyclic symlink target: {link.relative_to(sysroot)} -> {target}"
            )
        visited_links.add(candidate)
        hops += 1
        if hops > MAX_SYMLINK_HOPS:
            raise ValueError(
                f"symlink target exceeds {MAX_SYMLINK_HOPS} hops: "
                f"{link.relative_to(sysroot)} -> {target}"
            )

        nested_target = os.readlink(candidate)
        if os.path.isabs(nested_target):
            raise ValueError(
                "unsafe symlink chain reaches absolute target: "
                f"{link.relative_to(sysroot)} -> {candidate.relative_to(sysroot)} "
                f"-> {nested_target}"
            )
        pending.extendleft(reversed(Path(nested_target).parts))


def walk_tree(root: Path, relative_root: str) -> Iterator[Path]:
    """Yield a covered root and all descendants in bytewise path order."""
    start = root / relative_root
    if not start.exists() and not start.is_symlink():
        raise FileNotFoundError(f"required sysroot path is missing: {relative_root}")

    pending = [start]
    while pending:
        path = pending.pop()
        yield path
        if path.is_dir() and not path.is_symlink():
            children = list(path.iterdir())
            children.sort(key=lambda item: os.fsencode(item.name), reverse=True)
            pending.extend(children)


def manifest_records(sysroot: Path) -> Iterator[dict[str, str | None]]:
    """Yield canonical records for every entry in the covered sysroot trees."""
    for covered_root in COVERED_ROOTS:
        for path in walk_tree(sysroot, covered_root):
            metadata = path.lstat()
            kind = entry_type(metadata.st_mode)
            if kind == "symlink":
                resolve_symlink_within_sysroot(sysroot, path)
            yield {
                "relative_path": path.relative_to(sysroot).as_posix(),
                "type": kind,
                "mode": f"{stat.S_IMODE(metadata.st_mode):04o}",
                "symlink_target": os.readlink(path) if kind == "symlink" else None,
                "sha256": sha256_file(path) if kind == "file" else None,
            }


def write_atomic(path: Path, content: bytes) -> None:
    """Atomically replace a file while keeping generated metadata host-owned."""
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", dir=path.parent
    )
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(content)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary_path, path)
    except BaseException:
        temporary_path.unlink(missing_ok=True)
        raise


def generate(sysroot: Path, manifest_path: Path, checksum_path: Path) -> None:
    """Generate the content manifest and its relative-name SHA-256 checksum."""
    lines = [
        json.dumps(record, ensure_ascii=True, separators=(",", ":"))
        for record in manifest_records(sysroot)
    ]
    manifest_content = ("\n".join(lines) + "\n").encode("utf-8")
    write_atomic(manifest_path, manifest_content)

    manifest_digest = hashlib.sha256(manifest_content).hexdigest()
    checksum_content = (
        f"{manifest_digest}  {manifest_path.name}\n".encode("ascii")
    )
    write_atomic(checksum_path, checksum_content)


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Generate a deterministic manifest for lib, usr/lib, and usr/include."
    )
    parser.add_argument("sysroot", type=Path)
    parser.add_argument(
        "--output",
        type=Path,
        help=f"manifest path (default: <sysroot>/.robot-control/{MANIFEST_NAME})",
    )
    parser.add_argument(
        "--checksum-output",
        type=Path,
        help=f"checksum path (default: next to manifest as {CHECKSUM_NAME})",
    )
    return parser.parse_args()


def main() -> int:
    """Run manifest generation."""
    arguments = parse_args()
    sysroot = arguments.sysroot.resolve(strict=True)
    manifest_path = (
        arguments.output
        if arguments.output is not None
        else sysroot / ".robot-control" / MANIFEST_NAME
    )
    manifest_path = manifest_path.resolve()
    checksum_path = (
        arguments.checksum_output
        if arguments.checksum_output is not None
        else manifest_path.with_name(CHECKSUM_NAME)
    )
    checksum_path = checksum_path.resolve()

    generate(sysroot, manifest_path, checksum_path)
    print(f"manifest={manifest_path}")
    print(f"checksum={checksum_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
