"""Preserve failed feedback acceptance separately from verified SBUS withdrawal/restoration."""
from pathlib import Path
import json
import runpy

BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[4]


def main():
    """Audit this consumed attempt without changing its original ±1.5rpm criterion."""
    log = (BASE/'recovery-target/application.log').read_text()
    analyzer = runpy.run_path(str(ROOT/'scripts/test/analyze_control_hil_trace.py'))
    trace = analyzer['analyze'](log)
    assert trace['feedback_bad'] and not trace['discontinuities']
    assert 'motion_window_ms=8000 ' in log and 'zero_feedback_tenths_rpm=15 ' in log
    assert 'source_fault=3 ' in log and 'event=motion_stop verified=1' in log
    causal = runpy.run_path(str(BASE/'analyze-sbus.py'))['check'](trace, 3, 8000)
    first, zero = causal['first_nonzero_ns'], causal['first_zero_ns']
    frames = []
    for line in (BASE/'recovery-target/candump.log').read_text().splitlines():
        f = line.split()
        assert f[1] == 'can0'
        payload = bytes.fromhex(' '.join(f[4:]))
        assert len(payload) == int(f[3][1:-1])
        frames.append((int(f[2],16), payload))
    independent = []
    for line in (BASE/'recovery-jcan-once/session.jsonl').read_text().splitlines():
        f = json.loads(line)
        if f.get('event') == 'frame':
            assert not any(f[k] for k in ('brs','extended','fd','remote'))
            independent.append((f['can_id'],bytes.fromhex(f['data_hex'])))
    assert frames == independent
    assert all(i in (0,0x181,0x201,0x281,0x381,0x481,0x581,0x601,0x701) for i,p in frames)
    for i,p in frames:
        if i == 0x201:
            assert len(p) == 6 and p[1] == 0 and p[0] in (0,2,6,7,15)
            assert p[2:4] == bytes(2) and 0 <= int.from_bytes(p[4:],'little',signed=True) <= 5
            assert p[4:] == bytes(2) or p[0] == 15
        if i == 0x281:
            assert p == bytes([3,0,0,0,0])
    for observations in ([(i,p) for _,i,p in trace['tx']],[(i,p) for _,i,p,_,_ in trace['rx']]):
        pos = 0
        for frame in observations:
            while pos < len(frames) and frames[pos] != frame:
                pos += 1
            assert pos < len(frames), frame
            pos += 1
    writes, readbacks, pending = [], {}, None
    for i,p in frames:
        if i == 0x601:
            assert pending is None and len(p) == 8
            pending = p
            if p[0] != 0x40:
                writes.append((int.from_bytes(p[1:3],'little'),p[3],int.from_bytes(p[4:],'little')))
        elif i == 0x581:
            assert pending and len(p) == 8 and p[1:4] == pending[1:4]
            if pending[0] == 0x40:
                width = {0x43:4,0x4b:2,0x4f:1}[p[0]]
                readbacks[int.from_bytes(p[1:3],'little'),p[3]] = int.from_bytes(p[4:4+width],'little')
            else:
                assert p[0] == 0x60
            pending = None
    rpdo = [(0x1400,1,0x80000201),(0x1600,0,0),(0x1600,1,0x60400010),
            (0x1600,2,0x60ff0320),(0x1600,0,2),(0x1400,1,0x201)]
    tpdo = [(0x1801,1,0x80000281),(0x1a01,0,0),(0x1a01,1,0x60610008),
            (0x1a01,2,0x603f0020),(0x1a01,0,2),(0x1801,5,100),(0x1801,1,0x281)]
    expected = [(0x1017,0,500),(0x2000,0,1000)] + rpdo + tpdo
    expected += [(0x60ff,1,0),(0x60ff,2,0),(0x6040,0,6),(0x60ff,1,0),(0x60ff,2,0),(0x6040,0,0)]
    expected += [(i,s,0x60600008 if (i,s)==(0x1600,2) else v) for i,s,v in rpdo]
    expected += [(i,s,0 if i==0x1a01 or (i,s)==(0x1801,5) else v) for i,s,v in tpdo]
    expected += [(0x2000,0,0),(0x1017,0,0)]
    assert pending is None and writes == expected
    assert [p.hex() for i,p in frames if i==0] == ['8001','0101','8001']
    for key,value in {(0x1017,0):0,(0x2000,0):0,(0x6040,0):0,(0x1400,1):0x201,
                      (0x1600,0):2,(0x1600,1):0x60400010,(0x1600,2):0x60600008,
                      (0x1801,1):0x281,(0x1801,2):255,(0x1801,5):0,
                      (0x1a01,0):0,(0x1a01,1):0,(0x1a01,2):0}.items():
        assert readbacks[key] == value
    speeds = [(t,int.from_bytes(p[4:6],'little',signed=True),int.from_bytes(p[6:8],'little',signed=True))
              for t,i,p,_,_ in trace['rx'] if i==0x181]
    assert all(abs(l) <= 15 and (abs(r) <= 15 if t < first else r <= 75) for t,l,r in speeds)
    outliers = [(t,l,r) for t,l,r in speeds if r < -15]
    assert len(outliers) == 1 and outliers[0][0] > zero and outliers[0][1:] == (0,-16)
    assert sum(r > 15 for t,l,r in speeds if first <= t < zero) >= 2
    since = reached = verified = None
    for t,l,r in speeds:
        if t <= zero:
            continue
        if abs(l) > 15 or abs(r) > 15:
            since = reached = None
        else:
            since = t if since is None else since
            if reached is not None and t > reached:
                verified = t
                break
            if t-since >= 150000000:
                reached = t
    assert verified and verified-zero <= 1000000000
    before = json.loads((BASE/'recovery-target/can-before.json').read_text())
    after = json.loads((BASE/'recovery-target/can-after.json').read_text())
    assert before['linkinfo']['info_xstats'] == after['linkinfo']['info_xstats']
    assert not any(after['linkinfo']['info_data']['berr_counter'].values())
    for side in ('rx','tx'):
        for key in ('errors','dropped','over_errors','carrier_errors','collisions'):
            assert before['stats64'][side].get(key,0) == after['stats64'][side].get(key,0)
    operator = json.loads((BASE/'operator-post-trial.json').read_text())
    assert operator['drive_power_off'] and operator['receiver_stayed_powered']
    capture = json.loads((BASE/'recovery-jcan-once/result.json').read_text())
    orchestration = json.loads((BASE/'recovery-orchestration.json').read_text())
    assert capture['passed'] and orchestration['passed']
    result = dict(status='FAILED_F4_FEEDBACK_LIMIT_WITH_VALID_SBUS_WITHDRAWAL',
                  original_band_tenths_rpm=15, causal=causal, matching_frames=len(frames),
                  rpdo_frames=sum(i==0x201 for i,p in frames), volatile_writes=len(writes),
                  baseline_restored=True, trace_records=trace['count'], trace_correlates_with_capture=True,
                  discontinuities=0, feedback_bad=True, raw_outliers=outliers,
                  outlier_after_first_zero_ms=(outliers[0][0]-zero)/1e6,
                  stable_verified_after_first_zero_ms=(verified-zero)/1e6,
                  counters_unchanged=True, both_markers_consumed=True, operator_OFF_confirmed=True,
                  startup_prompts=[line for line in log.splitlines() if line.startswith('event=input_status')],
                  positive_motion_peak_tenths_rpm=max(r for t,l,r in speeds),
                  scope='SBUS withdrawal timing verified; full F4 fails original feedback band; no threshold change or retry')
    (BASE/'failure-analysis.json').write_text(json.dumps(result,indent=2)+chr(10))
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
