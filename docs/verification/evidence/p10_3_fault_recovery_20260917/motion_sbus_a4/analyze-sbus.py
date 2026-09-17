"""Audit moving SBUS withdrawal on one application monotonic clock."""
from pathlib import Path
import json
import re
import runpy

BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[4]


def check(trace, fault, motion_window_ms=3000):
    """Reject late loss, voluntary stop, X1, resumed output and unrelated faults."""
    assert 3000 <= motion_window_ms <= 8000
    assert fault in (3, 4, 5), 'Expected frame_lost, failsafe or valid-frame timeout'
    targets = [(t, p) for t, i, p in trace['tx'] if i == 0x201]
    status = [(t, p) for t, i, p, _, _ in trace['rx'] if i == 0x181]
    assert all(len(p) == 6 and p[2:4] == bytes(2) and
               0 <= int.from_bytes(p[4:], 'little', signed=True) <= 5 for _, p in targets)
    first = next(t for t, p in targets if p[4:] != bytes(2))
    zero = next(t for t, p in targets if t > first and p[2:] == bytes(4))
    assert zero - first < (motion_window_ms - 50) * 1000000, 'Automatic cutoff is not SBUS fault acceptance'
    frames = trace['frames']
    faults = [f for f in frames if f['flags'] & 12]
    if fault in (3, 4):
        assert faults, 'Missing raw loss flags'
        stimulus = faults[0]['ns']
        kind = 'frame_lost' if fault == 3 else 'failsafe'
        assert any(f['flags'] & (4 if fault == 3 else 8) for f in faults if f['ns'] <= zero)
        withdrawn = [(t, d) for t, d in trace['cycles']
                     if stimulus <= t and d[15] == 4 and d[14] == 0]
    else:
        assert not any(f['flags'] & 12 and f['ns'] <= zero for f in frames)
        withdrawn = [(t, d) for t, d in trace['cycles'] if t >= first and d[15] == 4 and d[14] == 0]
        assert withdrawn
        _, d = withdrawn[0]
        last = max(f['ns'] for f in frames if f['ns'] <= zero)
        assert d[4] == last, 'Timeout must retain the last received frame timestamp'
        stimulus = last + 100000000
        kind = 'valid_frame_timeout_not_electrical_UART_silence'
    assert first < stimulus <= zero and zero - stimulus <= 100000000
    assert withdrawn and zero <= withdrawn[0][0] <= zero + 100000000
    d = withdrawn[0][1]
    assert d[9:13] == [0, 0, 0, 0], 'Source withdrawal must reach selected and approved targets'
    assert any(first <= t < stimulus and int.from_bytes(p[6:8], 'little', signed=True) > 15
               for t, p in status), 'Missing physical right motion before loss'
    assert all(len(p) == 8 and not (int.from_bytes(p[:2], 'little') & 0x8000)
               and not (int.from_bytes(p[2:4], 'little') & 0x8000) for _, p in status), 'X1 interfered'
    assert all(p[2:] == bytes(4) for t, p in targets if t >= stimulus), 'Nonzero after loss'
    assert all(d[14] == 0 and d[9:13] == [0, 0, 0, 0]
               for t, d in trace['cycles'] if t >= withdrawn[0][0]), 'Authority resumed'
    assert len(trace['stops']) == 1 and trace['stops'][0]['cause'] == 5
    return dict(kind=kind, first_nonzero_ns=first, loss_ns=stimulus, first_zero_ns=zero,
                loss_to_zero_ms=(zero-stimulus)/1e6, nonzero_window_ms=(zero-first)/1e6,
                flags_observed=sorted({f['flags'] for f in frames}),
                timing_scope='Reader batch receive or valid-frame deadline to successful RPDO syscall')


def main():
    """Require complete trace, bounded standstill, dual capture and full restoration."""
    log = (BASE/'recovery-target/application.log').read_text()
    analyzer = runpy.run_path(str(ROOT/'scripts/test/analyze_control_hil_trace.py'))
    trace = analyzer['analyze'](log)
    assert not trace['feedback_bad'] and not trace['discontinuities']
    header = next(line for line in log.splitlines() if line.startswith('event=trace_header'))
    assert 'zero_feedback_tenths_rpm=15 ' in header
    failures = [line for line in log.splitlines() if 'phase=control ok=0' in line]
    assert len(failures) == 1 and 'control_hil_motion_envelope' in failures[0]
    fault = re.search(r'source_fault=(\d+)', failures[0])
    assert fault
    assert 'motion_window_ms=8000 ' in log and 'maximum_nonzero_ms=8000' in log
    result = check(trace, int(fault[1]), 8000)
    result['feedback'] = analyzer['analyze_motion_feedback'](trace, 15, True, 8000)
    runpy.run_path(str(BASE/'analyze-protocol.py'))['main']()
    result.update(status='PASS_F4_WIRE_PENDING_OPERATOR', protocol=json.loads((BASE/'protocol-analysis.json').read_text()))
    (BASE/'sbus-analysis.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
