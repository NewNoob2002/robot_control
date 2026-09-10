#!/usr/bin/env python3
"""Verify the Phase 6 archive, retained trial manifests and checkpoint links without hardware access."""
import ast
import hashlib
import json
from pathlib import Path
import re
import tarfile

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / 'docs/verification/evidence'


def digest(data):
    """Return the SHA256 of the supplied evidence bytes."""
    return hashlib.sha256(data).hexdigest()


def contained(base, relative):
    """Resolve one manifest path while rejecting traversal or an absolute path."""
    path = Path(relative)
    assert not path.is_absolute() and '..' not in path.parts, relative
    result = (base / path).resolve()
    assert result.is_relative_to(base.resolve()), relative
    return result


def main():
    """Validate archive membership, retained hashes, Python syntax and navigation links."""
    manifest = json.loads((EVIDENCE / 'archives/MANIFEST.json').read_text())
    archived = 0
    for record in manifest['archives']:
        archive_path = contained(EVIDENCE, record['path'])
        assert digest(archive_path.read_bytes()) == record['sha256'], archive_path
        expected = {entry['path']: entry for entry in record['files']}
        assert len(expected) == len(record['files']), archive_path
        with tarfile.open(archive_path) as archive:
            members = archive.getmembers()
            assert len(members) == len(expected), archive_path
            seen = set()
            for member in members:
                contained(EVIDENCE, member.name)
                assert member.isfile() and member.name not in seen, member.name
                seen.add(member.name)
                data = archive.extractfile(member).read()
                entry = expected[member.name]
                assert len(data) == entry['bytes'] and digest(data) == entry['sha256'], member.name
                archived += 1
        assert seen == expected.keys(), archive_path
    for duplicate in manifest['removed_duplicates']:
        assert not contained(EVIDENCE, duplicate['removed']).exists(), duplicate
        assert digest(contained(EVIDENCE, duplicate['canonical']).read_bytes()) == duplicate['sha256'], duplicate
    retained = 0
    for checksums in sorted(EVIDENCE.glob('p6*/SHA256SUMS')):
        for line in checksums.read_text().splitlines():
            expected, name = line.split('  ', 1)
            base = ROOT if name.startswith('docs/') else checksums.parent
            path = contained(base, name)
            assert digest(path.read_bytes()) == expected, path
            retained += 1
    for path in EVIDENCE.glob('p6*/**/*.py'):
        ast.parse(path.read_text(), filename=str(path))
    ast.parse(Path(__file__).read_text())
    for relative in ('docs/verification/PHASE6_CHECKPOINT.md', 'docs/verification/P6_7_CLOSURE_BASELINE.md',
                     'docs/verification/evidence/README.md'):
        document = ROOT / relative
        for target in re.findall(r'\]\(([^)]+)\)', document.read_text()):
            target = target.split('#', 1)[0]
            if target and '://' not in target:
                assert (document.parent / target).exists(), (relative, target)
    print(f'PASS: {archived} archived files, {retained} retained checksums, duplicate replacements, syntax and checkpoint links')


if __name__ == '__main__':
    main()
