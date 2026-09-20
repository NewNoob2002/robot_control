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


def check_loss(log, result):
    """Require actual RF flags during dual motion, before cutoff, and immediate lasting withdrawal."""
    fault_lines=[line for line in log.splitlines() if line.startswith('phase=control ok=0')]
    assert len(fault_lines)==1 and 'operation=control_hil_motion_envelope' in fault_lines[0]
    fault=int(fields(fault_lines[0])['source_fault'])
    assert fault in (3,4), 'RF flags required; timeout alone is not RF-off evidence'
    assert result['discontinuities']==0, 'UART discontinuity is not this stimulus'
    moving=[(t,p) for t,i,p in result['tx'] if i==0x201 and p[2:]!=bytes(4)]
    first=moving[0][0]
    zero=next(t for t,i,p in result['tx'] if i==0x201 and t>first and p[2:]==bytes(4))
    assert 0<zero-first<2950000000, 'automatic cutoff is not RF-loss acceptance'
    flagged=[f for f in result['frames'] if f['flags']&12]
    assert flagged and all(f['ns']>first for f in flagged), 'RF loss before motion'
    stimulus=flagged[0]['ns']
    assert first<stimulus<=zero and zero-stimulus<=100000000
    assert any(f['flags'] & (4 if fault==3 else 8) for f in flagged if f['ns']<=zero)
    withdrawn=[(t,d) for t,d in result['cycles'] if t>=stimulus and d[15]==4 and d[14]==0]
    assert withdrawn and zero<=withdrawn[0][0]<=zero+100000000
    assert all(d[9:13]==[0,0,0,0] and not d[14] for t,d in result['cycles'] if t>=withdrawn[0][0])
    assert all(p[2:]==bytes(4) for t,i,p in result['tx'] if i==0x201 and t>=stimulus)
    assert len(result['stops'])==1 and result['stops'][0]['cause']==5
    assert sum(first<=t<stimulus and int.from_bytes(p[4:6],'little',signed=True)>20 and int.from_bytes(p[6:8],'little',signed=True)<-20 for t,i,p,_,_ in result['rx'] if i==0x181)>=2
    assert all(not int.from_bytes(p[:4],'little')&0x80008000 for _,i,p,_,_ in result['rx'] if i==0x181)
    return dict(kind='frame_lost' if fault==3 else 'failsafe',fault=fault,loss_ns=stimulus,
                first_nonzero_ns=first,first_zero_ns=zero,loss_to_zero_us=(zero-stimulus)/1000,
                flags_observed=sorted({f['flags'] for f in result['frames']}),no_restart=True,
                scope='Reader RF flags to successful zero RPDO; physical button/RF timing not measured')


def main():
    """Preserve evidence and require independent protocol, timing, counters and operator acceptance."""
    log=(BASE/'physical-once/application.log').read_text()
    summary=fields(next(line for line in log.splitlines() if line.startswith('event=summary ')))
    assert summary['primary_ok']=='0' and summary['restore_ok']=='1', summary
    result=analyze(log)
    assert result['right_sign']==-1, 'wrong physical binding'
    assert all(d[14] for _,d in result['cycles'] if d[11] or d[12])
    loss=check_loss(log,result)
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
    assert app['passed'] and app['application_exit']==1 and host['passed']
    operator=json.loads((BASE/'operator-off.json').read_text())
    assert operator['response']=='OFF' and operator['left_forward_and_stop_normal'] and operator['right_forward_and_stop_normal']
    assert operator['no_abnormal_sound'] and operator['drive_power_off'] and all(operator[k] for k in ('transmitter_off','receiver_continuously_powered','wiring_unchanged'))
    raw=[tuple(int.from_bytes(p[i:i+2],'little',signed=True) for i in (4,6)) for _,ident,p,_,_ in result['rx'] if ident==0x181]
    report=dict(status='PASS',loss=loss,right_sign=-1,profile=profile,summary=summary,feedback=feedback,capture=capture,
                motion_ms=(feedback['first_zero_ns']-feedback['first_nonzero_ns'])/1e6,
                stop_verified_ms=(feedback['stable_verified_ns']-feedback['first_zero_ns'])/1e6,
                raw_feedback_range_tenths_rpm=[[min(v[i] for v in raw),max(v[i] for v in raw)] for i in (0,1)],
                sbus_frames=len(result['frames']),trace_records=result['count'],can_counters_unchanged=True,
                operator=operator,scope='Dual-positive transmitter loss; protected exit1 expected; receiver powered; no recovery/rearm claim')
    (BASE/'analysis.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(report,ensure_ascii=False,indent=2))


if __name__=='__main__':
    main()
