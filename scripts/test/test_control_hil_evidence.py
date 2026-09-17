#!/usr/bin/env python3
"""Verify archived P10.3 evidence without executing trial runners or accessing devices."""
import ast
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / 'docs/verification/evidence'
DELIVERY = EVIDENCE / 'p10_3_delivery_20260917'


def verify(base, name, expected):
    """Check a repository-contained evidence file against its recorded digest."""
    relative = Path(name)
    assert not relative.is_absolute() and '..' not in relative.parts, name
    path = (base / relative).resolve()
    assert path.is_relative_to(base.resolve()) and path.is_file(), name
    assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, path


def main():
    """Check final inventory, historical trial checksums, script syntax and accepted F6 disposition."""
    inventory = json.loads((DELIVERY / 'evidence-manifest.json').read_text())
    for name, expected in inventory['sha256'].items():
        verify(ROOT, name, expected)
    count = 0
    for manifest in sorted(EVIDENCE.glob('p10_3*/**/SHA256SUMS')):
        for line in manifest.read_text().splitlines():
            expected, name = line.split('  ', 1)
            verify(ROOT if name.startswith('docs/') else manifest.parent, name, expected)
            count += 1
    scripts = list(EVIDENCE.glob('p10_3*/**/*.py'))
    for path in scripts:
        ast.parse(path.read_text(), filename=str(path))
    result = json.loads((EVIDENCE / 'p10_3_fault_recovery_20260917/motion_sigterm_a1/acceptance.json').read_text())
    assert result['status'] == 'PASS_F6_A1' and result['operator']['drive_power_off']
    assert result['target_runner_consumed'] and result['independent_capture_consumed']
    assert not result['feedback_bad'] and result['protocol']['baseline_restored']
    print(f"PASS: {len(inventory['sha256'])} archived files, {count} trial checksums, {len(scripts)} Python syntax checks; F6 accepted/OFF/consumed")


if __name__ == '__main__':
    main()
