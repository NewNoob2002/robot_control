#!/usr/bin/env python3
"""Replay immutable P9.2 event/timestamp traces through P9.3, without devices."""
import gzip
from pathlib import Path
import subprocess
import sys


def replay(binary, path, expected):
    """Preserve every read boundary/rejection; compare independent archived counts."""
    batches = []
    for line in gzip.decompress(path.read_bytes()).decode().splitlines():
        if line.startswith('event=read '):
            fields = dict(token.split('=', 1) for token in line.split())
            assert fields['discontinuity'] == '0'
            batches.append((fields['received_ns'], fields['session'], []))
        elif line.startswith('event=rejected '):
            assert batches
            batches[-1][2].append('r')
        elif line.startswith('event=frame '):
            fields = dict(token.split('=', 1) for token in line.split())
            assert batches and fields['session'] == batches[-1][1]
            channels = fields['channels'].strip(',').split(',')
            assert len(channels) == 16
            batches[-1][2].append('f ' + fields['flags'] + ' ' + ' '.join(channels))
    assert batches
    records = []
    for timestamp, session, events in batches:
        records.append(f'{timestamp} {session} {len(events)}')
        records.extend(events)
    result = subprocess.run([binary, '--replay'], input='\n'.join(records) + '\n',
                            text=True, capture_output=True, timeout=10, check=True)
    counts = tuple(map(int, result.stdout.split()))
    assert counts == expected, (path, counts, expected, result.stderr)
    print(f'{path.parent.name}: frames/rejected/lost/failsafe={counts} PASS (offline replay)')


def main():
    """Check static, historically failed, and successful traces without reclassifying HIL."""
    root = Path(__file__).resolve().parents[2] / 'docs/verification/evidence'
    replay(sys.argv[1], root / 'p9_2_sbus_capture_20260916/capture.log.gz', (1428, 0, 0, 0))
    manual = root / 'p9_2_sbus_manual_20260916'
    replay(sys.argv[1], manual / 'attempt1/capture.log.gz', (4098, 2, 422, 351))
    replay(sys.argv[1], manual / 'attempt2/capture.log.gz', (4290, 3, 420, 348))


if __name__ == '__main__':
    main()
