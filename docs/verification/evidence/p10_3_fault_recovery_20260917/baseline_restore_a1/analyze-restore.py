"""Independently audit the exact disabled-only restoration of A2's residual configuration."""
from collections import Counter
from pathlib import Path
import json
import runpy

BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[4]


def main():
    """Require paired captures, zero commands, exact18 writes and complete baseline readbacks."""
    log = (BASE / 'recovery-target/application.log').read_text()
    module = runpy.run_path(str(ROOT / 'scripts/test/analyze_control_hil_trace.py'))
    trace = module['analyze'](log)
    assert not trace['frames'] and not trace['cycles'] and not trace['feedback_bad']
    assert 'phase=baseline_restore ok=1' in log and 'event=control_start' not in log
    records = []
    for line in (BASE / 'recovery-target/candump.log').read_text().splitlines():
        f = line.split()
        payload = bytes.fromhex(' '.join(f[4:]))
        assert f[1] == 'can0' and len(payload) == int(f[3][1:-1])
        records.append((float(f[0][1:-1]), int(f[2], 16), payload))
    independent = []
    for line in (BASE / 'recovery-jcan-once/session.jsonl').read_text().splitlines():
        d = json.loads(line)
        if d.get('event') == 'frame':
            assert not any(d[k] for k in ('brs', 'extended', 'fd', 'remote'))
            independent.append((d['can_id'], bytes.fromhex(d['data_hex'])))
    target = [(i, p) for _, i, p in records]
    offsets = [i for i in range(len(independent)-len(target)+1) if independent[i:i+len(target)] == target]
    assert len(offsets) == 1
    assert Counter((i, p) for _, i, p in trace['tx']) == Counter((i, p) for i, p in target if i in (0, 0x201, 0x601))
    pending = None
    reads, writes, nmts, speed_rounds = {}, [], [], []
    for stamp, ident, payload in records:
        assert ident in (0, 0x181, 0x281, 0x381, 0x481, 0x581, 0x601, 0x701)
        if ident == 0:
            nmts.append((stamp, payload.hex()))
        elif ident == 0x181:
            assert len(payload) == 8 and int.from_bytes(payload[:4], 'little') & 0x80008000 == 0
            assert all(abs(int.from_bytes(payload[o:o+2], 'little', signed=True)) <= 10 for o in (4, 6))
        elif ident == 0x281:
            assert payload == bytes.fromhex('0300000000')
        elif ident == 0x601:
            assert pending is None and len(payload) == 8
            pending = payload
            if payload[0] != 0x40:
                assert reads[(0x605a, 0)] == 5
                writes.append((stamp, int.from_bytes(payload[1:3], 'little'), payload[3], int.from_bytes(payload[4:], 'little')))
        elif ident == 0x581:
            assert pending is not None and len(payload) == 8 and payload[1:4] == pending[1:4]
            key = int.from_bytes(payload[1:3], 'little'), payload[3]
            if pending[0] == 0x40:
                width = {0x43: 4, 0x4b: 2, 0x4f: 1}[payload[0]]
                reads[key] = int.from_bytes(payload[4:4+width], 'little')
                if key[0] == 0x606c:
                    data = payload[4:8]
                    assert all(abs(int.from_bytes(p, 'little', signed=True)) <= 10
                               for p in ([data[:2], data[2:]] if key[1] == 3 else [data]))
                    if key[1] == 3:
                        speed_rounds.append(stamp)
            else:
                assert payload[0] == 0x60
            pending = None
    rpdo = [(0x1400, 1, 0x80000201), (0x1600, 0, 0), (0x1600, 1, 0x60400010),
            (0x1600, 2, 0x60600008), (0x1600, 0, 2), (0x1400, 1, 0x201)]
    tpdo = [(0x1801, 1, 0x80000281), (0x1a01, 0, 0), (0x1a01, 1, 0),
            (0x1a01, 2, 0), (0x1a01, 0, 0), (0x1801, 5, 0), (0x1801, 1, 0x281)]
    expected = [(0x60ff, 1, 0), (0x60ff, 2, 0), (0x6040, 0, 0)] + rpdo + tpdo + [(0x2000, 0, 0), (0x1017, 0, 0)]
    assert pending is None and [(i, s, v) for _, i, s, v in writes] == expected
    assert [p for _, p in nmts] == ['8001']
    baseline = {(0x6060, 0): 3, (0x6061, 0): 3, (0x603f, 0): 0, (0x6040, 0): 0,
                (0x60ff, 1): 0, (0x60ff, 2): 0, (0x200f, 0): 1, (0x2000, 0): 0, (0x1017, 0): 0,
                (0x1400, 1): 0x201, (0x1400, 2): 255, (0x1400, 5): 1000,
                (0x1600, 0): 2, (0x1600, 1): 0x60400010, (0x1600, 2): 0x60600008,
                (0x1800, 1): 0x181, (0x1800, 2): 255, (0x1800, 5): 100,
                (0x1a00, 0): 2, (0x1a00, 1): 0x60410020, (0x1a00, 2): 0x606c0320,
                (0x1801, 1): 0x281, (0x1801, 2): 255, (0x1801, 5): 0,
                (0x1a01, 0): 0, (0x1a01, 1): 0, (0x1a01, 2): 0, (0x605a, 0): 5}
    assert all(reads[k] == v for k, v in baseline.items())
    assert all((reads[0x6041, 0] >> shift) & 0x4f == 0x40 for shift in (0, 16))
    holds = [[t for t in speed_rounds if t < writes[0][0]],
             [t for t in speed_rounds if writes[0][0] < t < nmts[0][0]],
             [t for t in speed_rounds if t > writes[-1][0]]]
    assert all(len(times) >= 2 and times[-1]-times[0] >= .150 for times in holds)
    application = json.loads((BASE / 'recovery-target/result.json').read_text())
    assert application['passed'] and application['application_exit'] == 0
    result = dict(status='PASS_DISABLED_ONLY_BASELINE_RESTORATION', matching_target_frames=len(records),
                  jcan_frames=len(independent), capture_offset=offsets[0], exact_writes=len(writes),
                  no_enable_or_rpdo=True, baseline_restored=True, trace_records=trace['count'],
                  standstill_holds_ms=[(x[-1]-x[0])*1000 for x in holds],
                  operator='Separate physical disposition required; no X1 recovery acceptance implied')
    (BASE / 'restoration-analysis.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
