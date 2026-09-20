"""Run the existing signed-motion and exact capture auditors, then verify trial disposition."""
import json
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parent
ROOT = next(p for p in BASE.parents if (p/'scripts/test/analyze_control_hil_trace.py').exists())
sys.path.insert(0,str(ROOT/'scripts/test'))
sys.path.insert(0,str(ROOT/'scripts/hil'))
from analyze_control_hil_trace import analyze, analyze_g3_feedback, analyze_g3_captures, fields
from phase6_zero_motion_soak import parse_capture


def main():
    """Verify protocol and cleanup independently; preserve the operator-confirmed physical direction failure."""
    log=(BASE/'physical-once/application.log').read_text()
    summary=fields(next(line for line in log.splitlines() if line.startswith('event=summary ')))
    assert summary['primary_ok']==summary['restore_ok']=='1', summary
    result=analyze(log)
    assert result['discontinuities']==0 and all(d[14] for _,d in result['cycles'] if d[11] or d[12])
    assert result['stops'][0]['cause'] in (1,2), 'unexpected normal-motion stop'
    profile='dual-positive'
    feedback=analyze_g3_feedback(result,profile,20)
    wire=parse_capture((BASE/'physical-once/candump.log').read_text())
    independent=[]
    for line in (BASE/'jcan/session.jsonl').read_text().splitlines():
        frame=json.loads(line)
        if frame.get('event')=='frame':
            assert not any(frame[k] for k in ('brs','extended','fd','remote'))
            independent.append((frame['can_id'],bytes.fromhex(frame['data_hex'])))
    capture=analyze_g3_captures(result,wire,independent,profile,20)
    before=json.loads((BASE/'physical-once/can-before.json').read_text())
    after=json.loads((BASE/'physical-once/can-final.json').read_text())
    assert before['linkinfo']==after['linkinfo']
    for side in ('rx','tx'):
        for key,value in before['stats64'][side].items():
            if key not in ('bytes','packets'): assert after['stats64'][side][key]==value
    app=json.loads((BASE/'physical-once/result.json').read_text())
    host=json.loads((BASE/'result.json').read_text())
    assert app['passed'] and app['application_exit']==0 and host['passed']
    operator=json.loads((BASE/'operator-clarification.json').read_text())
    assert operator['left_vehicle_forward'] and operator['right_vehicle_backward'] and not operator['right_vehicle_forward']
    assert operator['drive_power_off'] and operator['wheels_stationary'] and operator['no_abnormal_sound']
    raw=[tuple(int.from_bytes(p[i:i+2],'little',signed=True) for i in (4,6)) for _,ident,p,_,_ in result['rx'] if ident==0x181]
    report=dict(status='FAILED_PHYSICAL_DIRECTION', protocol_software_status='PASS',profile=profile,summary=summary,feedback=feedback,capture=capture,
                motion_ms=(feedback['first_zero_ns']-feedback['first_nonzero_ns'])/1e6,
                stop_verified_ms=(feedback['stable_verified_ns']-feedback['first_zero_ns'])/1e6,
                raw_feedback_range_tenths_rpm=[[min(v[i] for v in raw),max(v[i] for v in raw)] for i in (0,1)],
                sbus_frames=len(result['frames']),trace_records=result['count'],can_counters_unchanged=True,
                operator=operator,scope='Logical signed targets/feedback and cleanup pass; operator confirms right physical direction wrong; no motion acceptance or retry')
    (BASE/'direction-analysis.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(report,ensure_ascii=False,indent=2))


if __name__=='__main__':
    main()
