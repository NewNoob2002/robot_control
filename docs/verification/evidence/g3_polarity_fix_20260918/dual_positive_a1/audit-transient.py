"""Audit corrected dual-forward motion and the explicitly approved stop transient separately."""
import importlib.util
import json
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parent
ROOT = next(p for p in BASE.parents if (p/'scripts/test/analyze_control_hil_trace.py').exists())
sys.path.insert(0, str(ROOT/'scripts/test'))
sys.path.insert(0, str(ROOT/'scripts/hil'))
from analyze_control_hil_trace import analyze, fields
from phase6_zero_motion_soak import parse_capture


def audit_motion(tx, rx):
    """Check signed raw commands, active feedback, each transient and a fresh bounded stop."""
    commands = [(t,p) for t,i,p in tx if i == 0x201]
    first = next(t for t,p in commands if any(p[2:]))
    zero = next(t for t,p in commands if t > first and p[2:] == bytes(4))
    assert 0 < zero-first < 3000000000
    for t,p in commands:
        assert len(p) == 6
        left = int.from_bytes(p[2:4], 'little', signed=True)
        right = int.from_bytes(p[4:6], 'little', signed=True)
        assert (left == right == 0) or (1 <= left <= 5 and right == -left and p[:2] == bytes([15,0]))
        if t >= zero: assert left == right == 0
    speeds = [(t,int.from_bytes(p[4:6], 'little', signed=True),int.from_bytes(p[6:8], 'little', signed=True))
              for t,i,p in rx if i == 0x181]
    assert speeds and all(abs(l) <= 75 and abs(r) <= 75 for _,l,r in speeds)
    violations = []
    simultaneous = 0
    since = reached = verified = None
    for t,left,right_raw in speeds:
        right = -right_raw
        if t < first:
            assert abs(left) <= 20 and abs(right) <= 20
        elif t < zero:
            assert -20 <= left <= 75 and -20 <= right <= 75
            simultaneous += left > 20 and right > 20
        elif left < -20 or right < -20:
            violations.append(dict(after_zero_ms=(t-zero)/1e6,left_tenths=left,
                                   right_raw_tenths=right_raw,right_logical_tenths=right))
        if t <= zero: continue
        if abs(left) > 20 or abs(right) > 20:
            since = reached = None
            assert verified is None, 'excursion after verified stable stop'
            continue
        since = t if since is None else since
        if verified is None and reached is not None and t > reached:
            verified = t
        if t-since >= 150000000: reached = t
    assert simultaneous >= 2 and verified is not None and verified-zero <= 1000000000
    # This is the observed trial, not a new universal frame-ignore threshold.
    assert len(violations) == 1 and violations[0]['right_logical_tenths'] == -25
    assert violations[0]['left_tenths'] == 8 and 0 < violations[0]['after_zero_ms'] < (verified-zero)/1e6
    return dict(motion_ms=(zero-first)/1e6,stop_verified_ms=(verified-zero)/1e6,
                simultaneous_motion_samples=simultaneous,violations=violations,
                no_restart=True,raw_targets='left=+1..+5,right=-left',
                raw_feedback_range_tenths_rpm=[[min(v[i] for v in speeds),max(v[i] for v in speeds)] for i in (1,2)])


def main():
    """Preserve original exit/failure while independently applying the existing acceptance rule."""
    log = (BASE/'physical-once/application.log').read_text()
    r = analyze(log)
    assert r['right_sign'] == -1 and r['motion_mode'] == 6 and r['zero_tolerance'] == 20
    assert r['feedback_bad'] and r['discontinuities'] == 0 and len(r['stops']) == 1
    assert r['stops'][0]['cause'] == 2
    assert all(d[14] for _,d in r['cycles'] if d[11] or d[12])
    summary = fields(next(l for l in log.splitlines() if l.startswith('event=summary ')))
    assert summary['primary_ok'] == '0' and summary['restore_ok'] == '1'
    assert 'operation=control_hil_trace_failure context=stopping feedback or evidence' in log
    app = json.loads((BASE/'physical-once/result.json').read_text())
    host = json.loads((BASE/'result.json').read_text())
    assert app['application_exit'] == 1 and not app['passed'] and not host['passed'] and host['capture_clean']
    wire = parse_capture((BASE/'physical-once/candump.log').read_text())
    independent = []
    for line in (BASE/'jcan/session.jsonl').read_text().splitlines():
        f = json.loads(line)
        if f.get('event') == 'frame':
            assert not any(f[k] for k in ('brs','extended','fd','remote'))
            independent.append((f['can_id'],bytes.fromhex(f['data_hex'])))
    old = ROOT/'docs/verification/evidence/g3_raised_motion_20260918/right_negative_a1/audit-failure.py'
    spec = importlib.util.spec_from_file_location('protocol_only',old)
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    protocol = audit.audit_protocol(r,wire,independent,'dual-positive',20)
    protocol['scope'] = 'Protocol/restoration only; motion and physical direction audited separately'
    motion = audit_motion(r['tx'],[(t,i,p) for t,i,p,_,_ in r['rx']])
    wire_motion = audit_motion([(round(t*1e9),i,p) for t,i,p in wire if i in (0,0x201,0x601)],
                               [(round(t*1e9),i,p) for t,i,p in wire if i not in (0,0x201,0x601)])
    before = json.loads((BASE/'physical-once/can-before.json').read_text())
    after = json.loads((BASE/'physical-once/can-final.json').read_text())
    assert before['linkinfo'] == after['linkinfo']
    for side in ('rx','tx'):
        for key,value in before['stats64'][side].items():
            if key not in ('bytes','packets'): assert after['stats64'][side][key] == value
    off = json.loads((BASE/'operator-off.json').read_text())
    assert off['response'] == 'OFF' and all(off[k] for k in ('left_forward_and_stop_normal',
        'right_forward_and_stop_normal','no_abnormal_sound','drive_power_off'))
    original = dict(status='FAILED_FEEDBACK_CRITERION',application_exit=1,summary=summary,
                    right_sign=-1,motion=motion,wire_motion=wire_motion,protocol=protocol,
                    operator=off,can_counters_unchanged=True)
    (BASE/'failure-analysis.json').write_text(json.dumps(original,ensure_ascii=False,indent=2)+chr(10))
    accepted = dict(original,status='PASS_USER_APPROVED_START_STOP_TRANSIENT',
        original_status=original['status'],classification='NORMAL_START_STOP_FEEDBACK_TRANSIENT',
        criterion='docs/plans/G3_RAISED_MOTION.md: approved startup/stop transient rule',
        physical_direction='Both wheels vehicle-forward confirmed by operator',
        limitation='Raised dual-forward only; original exit1 and feedback flags retained; online guard unchanged')
    (BASE/'analysis.json').write_text(json.dumps(accepted,ensure_ascii=False,indent=2)+chr(10))
    print(json.dumps(accepted,ensure_ascii=False,indent=2))


if __name__ == '__main__':
    main()
