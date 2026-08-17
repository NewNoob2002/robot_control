#!/usr/bin/env python3
"""Write an external identity lock for a validated robot-control sysroot."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import stat
import tempfile
from pathlib import Path


def sha256_file(path: Path) -> str:
    """Return the SHA-256 digest of one regular file."""
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_relative_checksum(path: Path, expected_name: str) -> str:
    """Read one strict sha256sum record with the expected relative file name."""
    fields = path.read_text(encoding="ascii").splitlines()
    if len(fields) != 1:
        raise ValueError(f"{path} must contain exactly one checksum record")
    digest, separator, name = fields[0].partition("  ")
    if (
        separator != "  "
        or len(digest) != 64
        or any(character not in "0123456789abcdef" for character in digest)
        or name != expected_name
    ):
        raise ValueError(f"{path} contains an invalid checksum record")
    return digest


def write_atomic(path: Path, document: dict[str, object]) -> None:
    """Atomically replace a JSON file in its destination directory."""
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", dir=path.parent
    )
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(document, output, indent=2, sort_keys=True)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary_path, path)
    except BaseException:
        temporary_path.unlink(missing_ok=True)
        raise


def resolve_output_without_following_final_symlink(requested: Path) -> Path:
    """Resolve an output parent while rejecting a symlink or non-file leaf."""
    try:
        output_mode = requested.lstat().st_mode
    except FileNotFoundError:
        pass
    else:
        if stat.S_ISLNK(output_mode) or not stat.S_ISREG(output_mode):
            raise ValueError(
                "sysroot identity lock output must be a regular non-symlink file"
            )

    if requested.name in {"", ".", ".."}:
        raise ValueError("sysroot identity lock output must name a file")
    return requested.parent.resolve() / requested.name


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Write an external JSON identity lock for a validated sysroot."
    )
    parser.add_argument("sysroot", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--artifact-id",
        help="stable artifact identifier (default: sysroot directory name)",
    )
    return parser.parse_args()


def main() -> int:
    """Generate the external sysroot lock."""
    arguments = parse_args()
    sysroot = arguments.sysroot.resolve(strict=True)
    output = resolve_output_without_following_final_symlink(arguments.output)
    metadata = sysroot / ".robot-control"
    content_checksum = metadata / "sysroot-content.sha256"
    target_metadata_checksum = metadata / "manifest.sha256"

    if output == sysroot or sysroot in output.parents:
        raise ValueError("sysroot identity lock must be stored outside the sysroot")

    architecture = (metadata / "architecture.txt").read_text(
        encoding="utf-8"
    ).strip()
    os_release: dict[str, str] = {}
    for line in (metadata / "os-release").read_text(encoding="utf-8").splitlines():
        key, separator, value = line.partition("=")
        if separator:
            os_release[key] = value.strip().strip('"')

    document: dict[str, object] = {
        "schema_version": 1,
        "artifact_id": arguments.artifact_id or sysroot.name,
        "architecture": architecture,
        "os": {
            "id": os_release.get("ID", ""),
            "version_id": os_release.get("VERSION_ID", ""),
        },
        "sysroot_content_sha256": read_relative_checksum(
            content_checksum, "sysroot-content.jsonl"
        ),
        "target_metadata_sha256": sha256_file(target_metadata_checksum),
    }
    write_atomic(output, document)
    print(f"lock={output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
