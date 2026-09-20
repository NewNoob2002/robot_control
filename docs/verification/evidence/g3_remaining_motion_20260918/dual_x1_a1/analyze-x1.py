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


def check(tx, rx):
    """Verify causal order on the application receive/send monotonic clock."""
    targets = [(t,p) for t,i,p in tx if i == 0x201]
    status = [(t,p) for t,i,p,_,_ in rx if i == 0x181]
    moving = [(t,p) for t,p in targets if p[4:] != bytes(2)]
    assert moving and all(0 < int.from_bytes(p[2:4],'little',signed=True) <= 5 and int.from_bytes(p[4:],'little',signed=True)==-int.from_bytes(p[2:4],'little',signed=True) for _,p in moving)
    first = moving[0][0]
    x1 = next(t for t,p in status if int.from_bytes(p[:2],'little') & 0x8000)
    zero = next(t for t,p in targets if t > first and p[2:] == bytes(4))
    assert first < x1 <= zero and zero-first < 2950000000, 'X1 must precede cutoff and first zero'
    assert 0 <= zero-x1 <= 100000000, 'X1-to-zero exceeds100ms'
    assert all(p[2:] == bytes(4) for t,p in targets if t >= x1), 'nonzero after X1'
    assert any(first <= t < x1 and int.from_bytes(p[4:6],'little',signed=True) > 20 and int.from_bytes(p[6:8],'little',signed=True) < -20 for t,p in status), 'no physical motion before X1'
    assert all(int.from_bytes(p[:2],'little') & 0x8000 for t,p in status if t >= x1), 'X1 released before end'
    return {'first_nonzero_ns':first,'x1_ns':x1,'first_zero_ns':zero,'x1_to_zero_ms':(zero-x1)/1e6,'nonzero_window_ms':(zero-first)/1e6}


def audit_protocol(result, wire, independent, profile, zero_tolerance=20):
    """Audit protocol/restoration only for a failed motion trial; never certify motion feedback."""
    assert wire and [(i,p) for _,i,p in wire] == independent, 'global capture order/payload mismatch'
    assert [(i,p) for _,i,p in result['tx']] == [(i,p) for _,i,p in wire if i in (0,0x201,0x601)]
    first,last,writes,nmts = {},{},{},[]
    downloads = []
    pending = None
    for stamp,ident,payload in wire:
        assert ident in (0,0x181,0x201,0x281,0x381,0x481,0x581,0x601,0x701)
        if ident == 0: nmts.append(payload.hex())
        elif ident == 0x201:
            assert payload[0] in (0,2,6,7,15) and payload[1] == 0
        elif ident == 0x281: assert payload == bytes([3,0,0,0,0])
        elif ident == 0x701: assert payload in (bytes([5]),bytes([127]))
        elif ident in (0x381,0x481): assert not payload
        elif ident == 0x181:
            assert int.from_bytes(payload[:2],'little') & 8 == 0 and int.from_bytes(payload[2:4],'little') & 0x8008 == 0
        elif ident == 0x601:
            assert len(payload) == 8 and pending is None
            pending = payload
            key = (int.from_bytes(payload[1:3],'little'),payload[3])
            if payload[0] != 0x40:
                assert payload[0] in (0x2f,0x2b,0x23)
                downloads.append((*key,int.from_bytes(payload[4:],'little')))
                assert key == (0x6040,0) or key in first, 'write without baseline readback'
                writes[key] = stamp
        elif ident == 0x581:
            assert pending is not None and len(payload) == 8 and pending[1:4] == payload[1:4]
            key = (int.from_bytes(payload[1:3],'little'),payload[3])
            if pending[0] == 0x40:
                assert payload[0] in (0x43,0x4b,0x4f)
                width = {0x43:4,0x4b:2,0x4f:1}[payload[0]]
                value = int.from_bytes(payload[4:4+width],'little')
                first.setdefault(key,value);last[key] = (stamp,value)
                if key[0] == 0x606c:
                    offsets = (4,6) if key[1] == 3 else (4,)
                    size = 2 if key[1] == 3 else 4
                    assert all(abs(int.from_bytes(payload[i:i+size],'little',signed=True)) <= zero_tolerance for i in offsets)
            else: assert payload[0] == 0x60
            pending = None
    rpdo = [(0x1400,1,0x80000201),(0x1600,0,0),(0x1600,1,0x60400010),
            (0x1600,2,0x60ff0320),(0x1600,0,2),(0x1400,1,0x201)]
    tpdo = [(0x1801,1,0x80000281),(0x1a01,0,0),(0x1a01,1,0x60610008),
            (0x1a01,2,0x603f0020),(0x1a01,0,2),(0x1801,5,100),(0x1801,1,0x281)]
    expected = [(0x1017,0,500),(0x2000,0,1000)] + rpdo + tpdo + [(0x60ff,1,0),(0x60ff,2,0),(0x6040,0,6)]
    expected += [(0x60ff,1,0),(0x60ff,2,0),(0x6040,0,0)]
    expected += [(i,s,0x60600008 if (i,s)==(0x1600,2) else v) for i,s,v in rpdo]
    expected += [(i,s,0 if i==0x1a01 or (i,s)==(0x1801,5) else v) for i,s,v in tpdo]
    expected += [(0x2000,0,0),(0x1017,0,0)]
    assert pending is None and downloads == expected and nmts == ['8001','0101','8001']
    assert all(last[key][0] > stamp and last[key][1] == first[key] for key,stamp in writes.items() if key != (0x6040,0))
    assert last[0x6040,0][1] == 0 and last[0x6040,0][0] > writes[0x6040,0]
    return dict(matching_frames=len(wire), volatile_writes=len(downloads), baseline_restored=True,
                scope='Protocol/restoration only; X1 accepted separately by causal audit')


def main():
    """Preserve evidence and require independent protocol, timing, counters and operator acceptance."""
    log=(BASE/'physical-once/application.log').read_text()
    summary=fields(next(line for line in log.splitlines() if line.startswith('event=summary ')))
    assert summary['primary_ok']=='0' and summary['restore_ok']=='1', summary
    result=analyze(log)
    assert result['right_sign']==-1, 'wrong physical binding'
    assert result['discontinuities']==0 and all(d[14] for _,d in result['cycles'] if d[11] or d[12])
    assert result['stops'][0]['cause'] in (1,4,5), 'X1 must cause first stop'
    x1_audit=check(result['tx'],result['rx'])
    assert all(f['flags'] & 12 == 0 for f in result['frames'] if f['ns'] <= x1_audit['x1_ns'])
    profile='dual-positive'
    feedback=analyze_g3_feedback(result,profile,20)
    wire=parse_capture((BASE/'physical-once/candump.log').read_text())
    independent=[]
    for line in (BASE/'jcan/session.jsonl').read_text().splitlines():
        frame=json.loads(line)
        if frame.get('event')=='frame':
            assert not any(frame[k] for k in ('brs','extended','fd','remote'))
            independent.append((frame['can_id'],bytes.fromhex(frame['data_hex'])))
    capture=audit_protocol(result,wire,independent,profile,20)
    wire_x1=check([(round(t*1e9),i,p) for t,i,p in wire if i in (0,0x201,0x601)],[(round(t*1e9),i,p,16,0) for t,i,p in wire if i not in (0,0x201,0x601)])
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
    assert operator['no_abnormal_sound'] and operator['drive_power_off'] and operator['x1_locked'] and operator['transmitter_receiver_online']
    raw=[tuple(int.from_bytes(p[i:i+2],'little',signed=True) for i in (4,6)) for _,ident,p,_,_ in result['rx'] if ident==0x181]
    report=dict(status='PASS',x1=x1_audit,wire_x1=wire_x1,right_sign=-1,profile=profile,summary=summary,feedback=feedback,capture=capture,
                motion_ms=(feedback['first_zero_ns']-feedback['first_nonzero_ns'])/1e6,
                stop_verified_ms=(feedback['stable_verified_ns']-feedback['first_zero_ns'])/1e6,
                raw_feedback_range_tenths_rpm=[[min(v[i] for v in raw),max(v[i] for v in raw)] for i in (0,1)],
                sbus_frames=len(result['frames']),trace_records=result['count'],can_counters_unchanged=True,
                operator=operator,scope='Dual-positive X1 raised/unloaded; protective exit1 accepted independently; no retry')
    (BASE/'analysis.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(report,ensure_ascii=False,indent=2))


if __name__=='__main__':
    main()
