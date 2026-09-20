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
    """Require source withdrawal during dual motion, before cutoff, without unrelated faults."""
    fault_line=next(l for l in log.splitlines() if l.startswith('phase=control ok=0'))
    assert 'operation=control_hil_motion_envelope' in fault_line
    fault=int(fields(fault_line)['source_fault'])
    assert fault in (5,9), 'only valid-frame timeout or expected partial discontinuity'
    batches=[]
    for line in log.splitlines():
        if line.startswith('event=trace_row '):
            row=fields(line)
            if row['kind']=='0': batches.append((int(row['ns']),[int(v) for v in row['fields'].split(',')]))
    moving=[(t,p) for t,i,p in result['tx'] if i==0x201 and p[2:]!=bytes(4)]
    assert moving
    first=moving[0][0]
    zero=next(t for t,i,p in result['tx'] if i==0x201 and t>first and p[2:]==bytes(4))
    assert 0<zero-first<2950000000, 'automatic cutoff is not signal-loss acceptance'
    boundaries=[(t,d) for t,d in batches if d[2]]
    if fault==9:
        assert len(boundaries)==1 and boundaries[0][1][2]==3
        stimulus=boundaries[0][0]
        kind='partial_timeout_revocation'
    else:
        assert not boundaries
        stimulus=max(f['ns'] for f in result['frames'] if f['ns']<=zero)+100000000
        kind='valid_frame_timeout'
    assert first<stimulus<=zero and zero-stimulus<=100000000
    last_raw=max(t for t,d in batches if t<=stimulus and d[3])
    assert first<last_raw<stimulus and stimulus-last_raw>=50000000
    assert not any(d[3] for t,d in batches if t>=stimulus), 'raw bytes resumed before trial exit'
    assert not any(f['flags']&12 for f in result['frames']), 'RF flags are not this stimulus'
    withdrawn=[(t,d) for t,d in result['cycles'] if t>=stimulus and d[15]==4 and d[14]==0]
    assert withdrawn and zero<=withdrawn[0][0]<=zero+100000000
    assert all(d[9:13]==[0,0,0,0] and not d[14] for t,d in result['cycles'] if t>=withdrawn[0][0])
    assert all(p[2:]==bytes(4) for t,i,p in result['tx'] if i==0x201 and t>=stimulus)
    assert len(result['stops'])==1 and result['stops'][0]['cause']==5
    assert sum(first<=t<stimulus and int.from_bytes(p[4:6],'little',signed=True)>20 and int.from_bytes(p[6:8],'little',signed=True)<-20 for t,i,p,_,_ in result['rx'] if i==0x181)>=2
    assert all(not int.from_bytes(p[:4],'little')&0x80008000 for _,i,p,_,_ in result['rx'] if i==0x181)
    return dict(kind=kind,fault=fault,last_raw_ns=last_raw,withdrawal_ns=stimulus,
                last_raw_to_withdrawal_ms=(stimulus-last_raw)/1e6,withdrawal_to_zero_us=(zero-stimulus)/1000,
                no_restart=True,scope='Reader-observed signal loss only; not voltage measurement or RF loss')


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
    assert operator['no_abnormal_sound'] and operator['drive_power_off'] and all(operator[k] for k in ('signal_only','power_ground_usb_unchanged','transmitter_receiver_power_on','signal_restored_after_power_off'))
    raw=[tuple(int.from_bytes(p[i:i+2],'little',signed=True) for i in (4,6)) for _,ident,p,_,_ in result['rx'] if ident==0x181]
    report=dict(status='PASS',loss=loss,right_sign=-1,profile=profile,summary=summary,feedback=feedback,capture=capture,
                motion_ms=(feedback['first_zero_ns']-feedback['first_nonzero_ns'])/1e6,
                stop_verified_ms=(feedback['stable_verified_ns']-feedback['first_zero_ns'])/1e6,
                raw_feedback_range_tenths_rpm=[[min(v[i] for v in raw),max(v[i] for v in raw)] for i in (0,1)],
                sbus_frames=len(result['frames']),trace_records=result['count'],can_counters_unchanged=True,
                operator=operator,scope='Dual-positive SBUS signal-only loss; protected exit1 expected; no recovery/rearm/RF-loss claim')
    (BASE/'analysis.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(report,ensure_ascii=False,indent=2))


if __name__=='__main__':
    main()
