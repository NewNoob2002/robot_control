"""Audit the failed right-negative attempt without changing its original feedback criterion."""
import json
import sys
from pathlib import Path
BASE=Path(__file__).resolve().parent
ROOT=next(p for p in BASE.parents if (p/'scripts/test/analyze_control_hil_trace.py').exists())
sys.path.insert(0,str(ROOT/'scripts/test'))
sys.path.insert(0,str(ROOT/'scripts/hil'))
from analyze_control_hil_trace import analyze, fields
from phase6_zero_motion_soak import parse_capture

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
            assert all(int.from_bytes(payload[i:i+2],'little') & 0x8008 == 0 for i in (0,2))
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
                scope='Protocol/restoration only; original right-negative feedback failure remains FAILED')


def main():
    """Retain failure and raw excursions while verifying the bounded stop and cleanup separately."""
    log=(BASE/'physical-once/application.log').read_text()
    r=analyze(log)
    summary=fields(next(line for line in log.splitlines() if line.startswith('event=summary ')))
    assert summary['primary_ok']=='0' and summary['restore_ok']=='1'
    assert r['feedback_bad'] and r['motion_mode']==5 and r['zero_tolerance']==20
    assert 'operation=control_hil_trace_failure context=stopping feedback or evidence' in log
    wire=parse_capture((BASE/'physical-once/candump.log').read_text())
    independent=[]
    for line in (BASE/'jcan/session.jsonl').read_text().splitlines():
        frame=json.loads(line)
        if frame.get('event')=='frame':
            assert not any(frame[k] for k in ('brs','extended','fd','remote'))
            independent.append((frame['can_id'],bytes.fromhex(frame['data_hex'])))
    protocol=audit_protocol(r,wire,independent,'single-right-negative',20)
    tx=[(t,b) for t,i,b in r['tx'] if i==0x201]
    assert all(len(b)==6 and int.from_bytes(b[2:4],'little',signed=True)==0 and -5<=int.from_bytes(b[4:6],'little',signed=True)<=0 for _,b in tx)
    first=next(t for t,b in tx if any(b[2:]))
    zero=next(t for t,b in tx if t>first and b[2:]==bytes(4))
    assert 0<zero-first<3000000000 and all(b[2:]==bytes(4) for t,b in tx if t>=zero)
    assert all(b[:2]==bytes([15,0]) for _,b in tx if any(b[2:]))
    speeds=[(t,int.from_bytes(b[4:6],'little',signed=True),int.from_bytes(b[6:8],'little',signed=True)) for t,i,b,_,_ in r['rx'] if i==0x181]
    violations=[dict(ns=t,after_zero_ms=(t-zero)/1e6,left_tenths=l,right_tenths=rr) for t,l,rr in speeds if abs(l)>20 or (abs(rr)>20 if t<first else rr < -75 or rr > 20)]
    wire_first=next(t for t,i,b in wire if i==0x201 and any(b[2:]))
    wire_zero=next(t for t,i,b in wire if t>wire_first and i==0x201 and b[2:]==bytes(4))
    wire_excursions=[dict(after_zero_ms=(t-wire_zero)*1000,right_tenths=int.from_bytes(b[6:8],'little',signed=True))
                     for t,i,b in wire if i==0x181 and t>wire_zero and int.from_bytes(b[6:8],'little',signed=True)>20]
    assert len(wire_excursions)==1 and wire_excursions[0]['right_tenths']==29
    assert len(violations)==1 and violations[0]['right_tenths']==29 and violations[0]['ns']>zero
    assert sum(rr < -20 for t,_,rr in speeds if first<=t<zero)>=2
    since=reached=verified=None
    for t,l,rr in speeds:
        if t<=zero: continue
        if abs(l)>20 or abs(rr)>20:
            since=reached=None
            continue
        since=t if since is None else since
        if reached is not None and t>reached:
            verified=t
            break
        if t-since>=150000000: reached=t
    assert verified is not None and verified-zero<=1000000000
    before=json.loads((BASE/'physical-once/can-before.json').read_text())
    after=json.loads((BASE/'physical-once/can-final.json').read_text())
    assert before['linkinfo']==after['linkinfo']
    for side in ('rx','tx'):
        for key,value in before['stats64'][side].items():
            if key not in ('bytes','packets'): assert after['stats64'][side][key]==value
    off=json.loads((BASE/'operator-off.json').read_text())
    assert off['response']=='OFF' and off['right_reverse_and_stop_normal'] and off['left_stationary'] and off['drive_power_off'] and off['no_abnormal_sound']
    out=dict(status='FAILED_FEEDBACK_CRITERION',protocol=protocol,summary=summary,
             wire_motion_ms=(wire_zero-wire_first)*1000,wire_excursions=wire_excursions,
             motion_ms=(zero-first)/1e6,stop_verified_ms=(verified-zero)/1e6,violations=violations,
             raw_feedback_range_tenths_rpm=[[min(v[i] for v in speeds),max(v[i] for v in speeds)] for i in (1,2)],
             no_positive_target=True,no_restart=True,stop_cause=r['stops'],can_counters_unchanged=True,operator=off,
             scope='Protocol/restoration and bounded eventual standstill verified; right-negative physical acceptance NOT granted; no retry')
    (BASE/'failure-analysis.json').write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(out,ensure_ascii=False,indent=2))


if __name__=='__main__':
    main()
