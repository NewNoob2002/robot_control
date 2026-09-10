#!/usr/bin/env python3
"""Reconcile manual TPDO mapping, passive speed samples and exact restoration without inferring motor timing."""
from collections import Counter
from decimal import Decimal
from pathlib import Path
import csv
import json
import re
import statistics

root=Path(__file__).resolve().parent
frames=[]
for line in (root/'target/rk3588_can.log').read_text().splitlines():
    match=re.fullmatch(r'\s*\((\d+\.\d+)\)\s+can0\s+([0-9A-Fa-f]+)\s+\[(\d+)\]\s*(.*)',line)
    assert match,line
    t,ident,dlc,payload=match.groups();data=bytes.fromhex(payload)
    assert len(data)==int(dlc)
    frames.append((Decimal(t),int(ident,16),data))
events=[json.loads(l) for l in (root/'jcan_session.jsonl').read_text().splitlines()]
assert all(e.get('ok') is not False and not e.get('warnings') and not e.get('error') for e in events)
jcan=[e for e in events if e.get('event')=='frame']
assert all(not any(e.get(k,False) for k in ('fd','brs','extended','remote')) for e in jcan)
assert [(i,d) for _,i,d in frames]==[(e['can_id'],bytes.fromhex(e['data_hex'])) for e in jcan]
assert any(e.get('id')==99 and e.get('ok') and e.get('data',{}).get('stopped') for e in events)
assert (root/'target/executor.rc').read_text()=='0'
assert (root/'target/capture.rc').read_text()=='0'
assert (root/'target/capture.stderr').stat().st_size==0
wrapper=json.loads((root/'target/wrapper_result.json').read_text())
assert wrapper['executor_stopped'] and wrapper['capture_stopped'] and wrapper['wrapper_exit']==0
assert json.loads((root/'coordinator_result.json').read_text())['error'] is None

nmt=[(t,d) for t,i,d in frames if i==0]
assert [d.hex() for _,d in nmt]==['8001','0101','8001']
active=nmt[1][0];restoring=nmt[2][0]
downloads=[(t,d) for t,i,d in frames if i==0x601 and d[0]!=0x40]
assert len(downloads)==18
setup=['2b171000f4010000','2300180181010080','2f001a0000000000','23001a0120036c60',
       '23001a0220004160','2f001a0002000000','2f001802ff000000','2b00180564000000','2300180181010000']
restore=['2300180181010080','2f001a0000000000','23001a0120004160','23001a0220036c60',
         '2f001a0002000000','2f001802ff000000','2b00180564000000','2300180181010000','2b17100000000000']
assert [d.hex() for _,d in downloads]==setup+restore
assert downloads[0][0]<nmt[0][0]<downloads[1][0]<downloads[8][0]<active<restoring<downloads[9][0]
assert not any(active<t<restoring for t,_ in downloads)
# Every request must complete before another request and have its exact echo/width.
pending=None;readbacks=[]
for t,i,d in frames:
    if i==0x601:
        assert pending is None,(pending,d)
        pending=(t,d)
    elif i==0x581:
        assert pending is not None,d
        request_t,request=pending;assert d[1:4]==request[1:4]
        if request[0]==0x40:
            assert d[0] in (0x4f,0x4b,0x43)
            width={0x4f:1,0x4b:2,0x43:4}[d[0]]
            readbacks.append((t,int.from_bytes(d[1:3],'little'),d[3],int.from_bytes(d[4:4+width],'little'),width))
        else:
            assert d[0]==0x60 and d[4:]==bytes(4)
        pending=None
assert pending is None
# Verify readback immediately following every download, including each rollback write.
for position,(t,d) in enumerate(downloads):
    index=int.from_bytes(d[1:3],'little');sub=d[3];width={0x2f:1,0x2b:2,0x23:4}[d[0]]
    following=next(r for r in readbacks if r[0]>t)
    assert following[1:]==(index,sub,int.from_bytes(d[4:4+width],'little'),width)

samples=[]
for t,i,d in frames:
    if i==0x181 and active<t<restoring:
        assert len(d)==8
        left=int.from_bytes(d[:2],'little',signed=True);right=int.from_bytes(d[2:4],'little',signed=True)
        status=int.from_bytes(d[4:],'little')
        assert all(((status>>shift)&0x6f) in (0x21,0x40,0x60) for shift in (0,16))
        samples.append((t,left,right,status))
assert samples and any(right for _,_,right,_ in samples)
intervals=[float((b[0]-a[0])*1000) for a,b in zip(samples,samples[1:])]
with (root/'speed_samples.csv').open('w') as out:
    writer=csv.writer(out);writer.writerow(['seconds_after_operational','left_raw_i16','right_raw_i16','status_hex'])
    writer.writerows((str(t-active),left,right,f'{status:08X}') for t,left,right,status in samples)
comparisons=[]
for t,index,sub,value,width in readbacks:
    if index==0x606c and sub==2 and active<t<restoring:
        signed=value if value<2**31 else value-2**32
        nearest=min(samples,key=lambda s:abs(s[0]-t))
        comparisons.append({'sdo_seconds_after_operational':float(t-active),'right_sdo_raw':signed,
                            'nearest_tpdo_raw':nearest[2],'tpdo_minus_sdo_ms':float((nearest[0]-t)*1000),
                            'raw_difference':nearest[2]-signed})
assert any(c['right_sdo_raw'] for c in comparisons)
final={(index,sub):value for _,index,sub,value,_ in readbacks}
for key,value in { (0x1800,1):0x181,(0x1800,2):255,(0x1800,5):100,(0x1a00,0):2,
                   (0x1a00,1):0x60410020,(0x1a00,2):0x606c0320,(0x1017,0):0,(0x60ff,1):0,(0x60ff,2):0}.items():
    assert final[key]==value,(key,final[key])
assert all(((final[(0x6041,0)]>>shift)&0x6f) in (0x21,0x40,0x60) for shift in (0,16))
pre=json.loads((root/'target/can_preflight.json').read_text())[0]
post=json.loads((root/'target/can_postflight.json').read_text())[0]
deltas={side:{key:post['stats64'][side][key]-pre['stats64'][side][key] for key in ('packets','bytes')} for side in ('rx','tx')}
counter_excess={}
for side in ('rx','tx'):
    selected=[d for _,i,d in frames if (i in (0,0x601))==(side=='tx')]
    counter_excess[side]={'packets':deltas[side]['packets']-len(selected),'bytes':deltas[side]['bytes']-sum(map(len,selected))}
    if side=='tx': assert counter_excess[side]=={'packets':0,'bytes':0}
    assert all(post['stats64'][side][key]==0 for key in ('errors','dropped'))
assert post['linkinfo']['info_data']['state']=='ERROR-ACTIVE'
assert all(v==0 for v in post['linkinfo']['info_xstats'].values())
assert json.loads((root/'jcan_config_pre.json').read_text())['data']==json.loads((root/'jcan_config_post.json').read_text())['data']
postflight=json.loads((root/'target_process_postflight.json').read_text())
assert not postflight['residual_processes'] and postflight['can'][0]['stats64']==post['stats64']
operator=root/'operator_observation.json'
result={'capture_and_restoration':'PASS','physical_acceptance': 'PASS: operator accepted manual TPDO speed feedback' if operator.exists() and json.loads(operator.read_text()).get('acceptance', {}).get('status') == 'PASS' else 'manual-speed acceptance pending',
        'scope_limit': 'RX accounting remains a separate unresolved evidence issue; enabled-drive watchdog timing not tested',
        'kernel_counter_excess':counter_excess,
        'counter_reconciliation':'PASS' if all(v==0 for values in counter_excess.values() for v in values.values()) else 'UNRESOLVED',
        'matched_frames':len(frames),'frame_ids':{f'{i:03X}':n for i,n in Counter(i for _,i,_ in frames).items()},
        'kernel_deltas':deltas,'target_or_controlword_writes':0,'downloads':18,'uploads':len(readbacks),'nmt':3,
        'operational_interval_ms':float((restoring-active)*1000),'tpdo_samples':len(samples),
        'tpdo_interval_ms':{'minimum':min(intervals),'median':statistics.median(intervals),'maximum':max(intervals)},
        'left_raw_range':[min(s[1] for s in samples),max(s[1] for s in samples)],
        'right_raw_range':[min(s[2] for s in samples),max(s[2] for s in samples)],
        'left_nonzero_samples':sum(s[1]!=0 for s in samples),'right_nonzero_samples':sum(s[2]!=0 for s in samples),
        'final_tpdo_samples':[[float(t-active),left,right,f'{status:08X}'] for t,left,right,status in samples[-5:]],
        'independent_right_comparisons':comparisons,
        'comparison_limitation':'SDO and TPDO samples are asynchronous; nearest-sample differences are not accuracy or scale proof.',
        'restored_mapping':['0x60410020','0x606C0320'],'heartbeat_restored':0,'event_timer_raw':100,
        'operator_observation':json.loads(operator.read_text()) if operator.exists() else 'pending'}
(root/'analysis.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='independent_right_comparisons'},indent=2))
