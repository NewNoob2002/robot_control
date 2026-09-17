"""Audit A2 zero-target feedback failure without granting recovery or restoration acceptance."""
from collections import Counter
from pathlib import Path
import json
import re
import runpy

BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[4]


def audit(records, independent, log):
    """Validate the shared capture interval, zero commands and exact incomplete cleanup."""
    assert independent[:len(records)] == [(i, p) for _, i, p in records]
    tail = independent[len(records):]
    assert len(tail) == 10
    assert all((i == 0x181 and p == bytes.fromhex('6014601400000000'))
               or (i == 0x281 and p == bytes.fromhex('0300000000'))
               or (i == 0x701 and p == b'\x05') for i, p in tail)
    trace = runpy.run_path(str(ROOT / 'scripts/test/analyze_control_hil_trace.py'))['analyze'](log)
    assert trace['feedback_bad'] and len(trace['stops']) == 1 and trace['stops'][0]['cause'] == 6
    assert Counter((i, p) for _, i, p in trace['tx']) == Counter((i, p) for _, i, p in records if i in (0, 0x201, 0x601))
    assert all(row['flags'] == 0 and row['mapped'] == (0, 0, 0, 0) for row in trace['frames'])
    assert all(d[5:13] == [0] * 8 for _, d in trace['cycles'])
    pending = None
    writes, nmts, velocities, commands = [], [], [], []
    reads = {}
    for stamp, ident, payload in records:
        assert ident in (0, 0x181, 0x201, 0x281, 0x381, 0x481, 0x581, 0x601, 0x701)
        if ident == 0:
            nmts.append(payload.hex())
        elif ident == 0x181:
            assert len(payload) == 8
            assert int.from_bytes(payload[:4], 'little') & 0x80008000 == 0
            velocities.append((stamp, int.from_bytes(payload[4:6], 'little', signed=True),
                               int.from_bytes(payload[6:8], 'little', signed=True)))
        elif ident == 0x201:
            assert len(payload) == 6 and payload[0] in (0, 2, 6, 7, 15) and payload[1:] == bytes(5)
            commands.append((stamp, payload[0]))
        elif ident == 0x281:
            assert payload == bytes.fromhex('0300000000')
        elif ident == 0x601:
            assert pending is None and len(payload) == 8
            pending = payload
            if payload[0] != 0x40:
                assert reads[(0x605a, 0)] == 5
                writes.append((int.from_bytes(payload[1:3], 'little'), payload[3], int.from_bytes(payload[4:], 'little')))
        elif ident == 0x581:
            assert pending is not None and payload[1:4] == pending[1:4] and len(payload) == 8
            if pending[0] == 0x40:
                width = {0x43: 4, 0x4b: 2, 0x4f: 1}[payload[0]]
                reads[int.from_bytes(payload[1:3], 'little'), payload[3]] = int.from_bytes(payload[4:4+width], 'little')
            else:
                assert payload[0] == 0x60
            pending = None
    rpdo = [(0x1400, 1, 0x80000201), (0x1600, 0, 0), (0x1600, 1, 0x60400010),
            (0x1600, 2, 0x60ff0320), (0x1600, 0, 2), (0x1400, 1, 0x201)]
    tpdo = [(0x1801, 1, 0x80000281), (0x1a01, 0, 0), (0x1a01, 1, 0x60610008),
            (0x1a01, 2, 0x603f0020), (0x1a01, 0, 2), (0x1801, 5, 100), (0x1801, 1, 0x281)]
    expected = [(0x1017, 0, 500), (0x2000, 0, 1000)] + rpdo + tpdo
    expected += [(0x60ff, 1, 0), (0x60ff, 2, 0), (0x6040, 0, 6),
                 (0x60ff, 1, 0), (0x60ff, 2, 0), (0x6040, 0, 0)]
    assert pending is None and writes == expected and nmts == ['8001', '0101']
    assert reads[(0x6040, 0)] == 0 and reads[(0x6041, 0)] == 0x14601460
    assert reads[(0x606c, 1)] == 0xffffffff
    assert all(right == 0 for _, _, right in velocities)
    nonzero = [(t, left) for t, left, _ in velocities if left != 0]
    assert [v for _, v in nonzero] == [3, 2, 4]
    first = nonzero[0][0]
    shutdown = next(t for t, word in commands if t > first and word == 6)
    zeros = [t for t, left, _ in velocities if t > nonzero[-1][0] and left == 0]
    assert zeros[-1] - zeros[0] >= .15
    assert 'primary_ok=0 restore_ok=0' in log and 'operation=qualification_nonzero_velocity' in log
    phases = re.findall(r'event=recovery phase=(\w+)', log)
    assert phases == ['fault_ready']
    ready = int(re.search(r'event=recovery phase=fault_ready.*at_ns=(\d+)', log)[1])
    return dict(status='FAILED_zero_target_nonzero_feedback_and_incomplete_restoration',
                matching_overlap_frames=len(records), independent_frames=len(independent),
                independent_extra_tail_frames=len(tail), full_capture_equal=False,
                trace_complete=True, trace_records=trace['count'], all_targets_zero=True,
                raw_input_neutral=True, x1_observed=False, right_feedback_zero=True,
                left_feedback_rpm=[v/10 for _, v in nonzero],
                fault_ready_to_stop_ms=(trace['stops'][0]['ns']-ready)/1e6,
                first_bad_feedback_to_shutdown_ms=(shutdown-first)*1000,
                first_stable_zero_after_bad_ms=(zeros[0]-first)*1000,
                observed_stable_zero_span_ms=(zeros[-1]-zeros[0])*1000,
                exact_volatile_writes=len(writes), zero_and_disable_voltage_readback=True,
                cleanup_606c_1_raw='0xffffffff (-1 signed)', baseline_restored=False,
                unrestored=['RPDO mapping', 'TPDO2 mapping/timer', 'watchdog1000ms', 'heartbeat500ms'],
                timing_scope='Target candump / application clocks only; JCAN supplies byte/order evidence',
                cause='Measured drive feedback fluctuation; physical movement versus estimation error unresolved')


def main():
    """Retain the failed verdict and reject command/trace/capture mutations offline."""
    log = (BASE / 'recovery-target/application.log').read_text()
    records = []
    for line in (BASE / 'recovery-target/candump.log').read_text().splitlines():
        f = line.split()
        payload = bytes.fromhex(' '.join(f[4:]))
        assert f[1] == 'can0' and len(payload) == int(f[3][1:-1])
        records.append((float(f[0][1:-1]), int(f[2], 16), payload))
    independent = []
    for line in (BASE / 'recovery-jcan-once/session.jsonl').read_text().splitlines():
        row = json.loads(line)
        if row.get('event') == 'frame':
            assert not any(row[k] for k in ('brs', 'extended', 'fd', 'remote'))
            independent.append((row['can_id'], bytes.fromhex(row['data_hex'])))
    result = audit(records, independent, log)
    altered = list(records)
    index = next(i for i, (_, ident, _) in enumerate(altered) if ident == 0x201)
    t, ident, _ = altered[index]
    altered[index] = (t, ident, bytes.fromhex('0f0001000000'))
    altered_independent = list(independent)
    altered_independent[index] = (ident, altered[index][2])
    for bad_records, bad_independent, bad_log in (
            (altered, altered_independent, log),
            (records, independent, log.replace('event=trace_end', 'event=missing_end')),
            (records, independent[:-1] + [(0x201, bytes(6))], log)):
        try:
            audit(bad_records, bad_independent, bad_log)
        except AssertionError:
            continue
        raise AssertionError('Invalid mutated evidence accepted')
    result['mutation_rejections'] = 3
    (BASE / 'failure-analysis.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
