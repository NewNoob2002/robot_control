"""Require X1 during right motion, causal zero output and no resumed motion."""
from pathlib import Path
import json
import runpy

BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[4]


def check(tx, rx):
    """Verify causal order on the application receive/send monotonic clock."""
    targets = [(t,p) for t,i,p in tx if i == 0x201]
    status = [(t,p) for t,i,p,_,_ in rx if i == 0x181]
    moving = [(t,p) for t,p in targets if p[4:] != bytes(2)]
    assert moving and all(p[2:4] == bytes(2) and 0 < int.from_bytes(p[4:],'little',signed=True) <= 5 for _,p in moving)
    first = moving[0][0]
    x1 = next(t for t,p in status if int.from_bytes(p[:2],'little') & 0x8000)
    zero = next(t for t,p in targets if t > first and p[2:] == bytes(4))
    assert first < x1 <= zero and zero-first < 2950000000, 'X1 must precede cutoff and first zero'
    assert 0 <= zero-x1 <= 100000000, 'X1-to-zero exceeds100ms'
    assert all(p[2:] == bytes(4) for t,p in targets if t >= x1), 'nonzero after X1'
    assert any(first <= t < x1 and int.from_bytes(p[6:8],'little',signed=True) > 15 for t,p in status), 'no physical motion before X1'
    assert all(int.from_bytes(p[:2],'little') & 0x8000 for t,p in status if t >= x1), 'X1 released before end'
    return {'first_nonzero_ns':first,'x1_ns':x1,'first_zero_ns':zero,'x1_to_zero_ms':(zero-x1)/1e6,'nonzero_window_ms':(zero-first)/1e6}


def main():
    """Combine causal trace verification with unchanged strict motion/restoration checks."""
    log = (BASE/'recovery-target/application.log').read_text()
    trace = runpy.run_path(str(ROOT/'scripts/test/analyze_control_hil_trace.py'))['analyze'](log)
    assert not trace['feedback_bad'] and not trace['discontinuities']
    assert len(trace['stops']) == 1 and trace['stops'][0]['cause'] in (1,4,5)
    header = next(line for line in log.splitlines() if line.startswith('event=trace_header'))
    assert 'zero_feedback_tenths_rpm=15 ' in header
    result = check(trace['tx'],trace['rx'])
    result['feedback'] = runpy.run_path(str(ROOT/'scripts/test/analyze_control_hil_trace.py'))['analyze_motion_feedback'](trace,15,True)
    assert 'control_hil_motion_envelope' in log or trace['stops'][0]['cause'] == 1
    runpy.run_path(str(BASE/'analyze-protocol.py'))['main']()
    result.update(status='PASS_F3_WIRE_PENDING_OPERATOR',protocol=json.loads((BASE/'protocol-analysis.json').read_text()))
    (BASE/'x1-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
