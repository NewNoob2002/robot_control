#!/usr/bin/env python3
"""Recheck archived raw SBUS bytes, phase observations and exact exit evidence."""
import collections
import gzip
import hashlib
import json
from pathlib import Path
import re
import statistics
import sys


def analyze(directory):
    """Return independent decoding and timing evidence; never infer operator timing."""
    root = Path(directory)
    content = gzip.decompress((root / 'capture.log.gz').read_bytes())
    timeline = [json.loads(line) for line in (root / 'timeline.jsonl').read_text().splitlines()]
    phases = [event for event in timeline if event['kind'] == 'phase']
    start = phases[0]['start_mono_ns']
    rows, reads, errors, rejected, summaries = [], [], [], [], []
    wire = bytearray()
    rejected_seconds = []
    marker = False
    for line in content.decode().splitlines():
        if line.startswith('event=read '):
            match = re.fullmatch(r'event=read session=(\d+) received_ns=(\d+) discontinuity=(\d+) kernel_raw=([0-9a-f]*)', line)
            assert match, line
            session, timestamp, discontinuity = map(int, match.group(1, 2, 3))
            reads.append((session, timestamp, discontinuity))
            assert discontinuity == 0, 'Cannot concatenate across a discontinuity'
            for byte in bytes.fromhex(match[4]):
                if marker:
                    assert byte == 255, 'Kernel line-error marker'
                    wire.append(255)
                    marker = False
                elif byte == 255:
                    marker = True
                else:
                    wire.append(byte)
        elif line.startswith('event=frame '):
            match = re.fullmatch(r'event=frame session=(\d+) sequence=(\d+) flags=(\d+) channels=([0-9,]+)', line)
            assert match and int(match[2]) == len(rows) + 1
            assert reads and reads[-1][0] == int(match[1])
            rows.append(dict(second=(reads[-1][1] - start) / 1e9, flags=int(match[3]),
                             channels=[int(x) for x in match[4].split(',') if x]))
        elif line.startswith('event=error '):
            errors.append(line)
        elif line.startswith('event=rejected '):
            rejected.append(line)
            rejected_seconds.append((reads[-1][1] - start) / 1e9)
        elif line.startswith('event=summary '):
            summaries.append(line)
        else:
            assert line.startswith('event=start '), line
    assert not marker
    # Offline candidate search includes startup fragments instead of assuming alignment.
    decoded_frames = []
    rejected_offsets = []
    offset = 0
    while offset < len(wire):
        header = wire.find(bytes([15]), offset)
        if header < 0 or header + 25 > len(wire):
            break
        if wire[header + 24] != 0:
            rejected_offsets.append(header)
            offset = header + 1
        else:
            decoded_frames.append(wire[header:header + 25])
            offset = header + 25
    assert len(decoded_frames) == len(rows)
    assert len(rejected_offsets) == len(rejected)
    for frame, observation in zip(decoded_frames, rows):
        assert frame[0] == 15 and frame[24] == 0 and frame[23] == observation['flags']
        packed = int.from_bytes(frame[1:23], 'little')
        assert [(packed >> (channel * 11)) & 2047 for channel in range(16)] == observation['channels']
    transitions, previous = [], None
    for row in rows:
        if row['flags'] != previous:
            transitions.append(dict(second=row['second'], flags=row['flags'], channels=row['channels']))
            previous = row['flags']
    intervals = [(b[1] - a[1]) / 1e6 for a, b in zip(reads, reads[1:])]
    phase_results = []
    for index, phase in enumerate(phases):
        low = phase['planned_second']
        high = phases[index + 1]['planned_second'] if index + 1 < len(phases) else 30
        selected = [row for row in rows if low <= row['second'] < high]
        phase_results.append(dict(prompt=phase['title'], planned_seconds=[low, high], frames=len(selected),
                                  channels_min_max=[[min(row['channels'][i] for row in selected), max(row['channels'][i] for row in selected)] for i in range(16)]))
    result = json.loads((root / 'result.json').read_text()) if (root / 'result.json').exists() else None
    relay = [json.loads(line) for line in (root / 'relay.jsonl').read_text().splitlines()]
    if result:
        assert result['exit_code'] == 143 and 30000 <= result['run_to_signal_ms'] < 30500
        assert result['signal_to_reaped_ms'] < 1000 and not errors
        assert summaries == [f"event=summary reason=signal signal=15 frames={len(rows)}"]
    return dict(capture_sha256=hashlib.sha256(content).hexdigest(), first_phase_utc=phases[0]['utc'],
                frames=len(rows), reads=len(reads), raw_wire_bytes=len(wire),
                independent_decode_matches=True, independent_rejected_offsets=rejected_offsets,
                nonframe_bytes=len(wire)-25*len(rows), rejected_seconds=rejected_seconds, sessions=sorted(set(r[0] for r in reads)),
                flags=dict(collections.Counter(row['flags'] for row in rows)),
                lost_frames=sum(bool(row['flags'] & 4) for row in rows),
                failsafe_frames=sum(bool(row['flags'] & 8) for row in rows),
                errors=errors, rejected=rejected, summaries=summaries,
                discontinuities=sum(bool(r[2]) for r in reads),
                notification_calls_succeeded=all(e['notification_exit_code'] == 0 for e in relay),
                channels_min_max=[[min(row['channels'][i] for row in rows), max(row['channels'][i] for row in rows)] for i in range(16)],
                healthy_ch7_values=sorted(set(row['channels'][6] for row in rows if row['flags'] == 0)),
                read_interval_ms=dict(min=min(intervals), median=statistics.median(intervals), max=max(intervals)),
                flag_transitions=transitions, phases=phase_results, target_exit=result,
                last_observation_second=rows[-1]['second'])


if __name__ == '__main__':
    print(json.dumps(analyze(sys.argv[1]), ensure_ascii=False, indent=2))
