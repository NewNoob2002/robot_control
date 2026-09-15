"""Audit the consumed v5 interface-loss trial from both raw captures, without hardware access."""
import json
from collections import Counter
from decimal import Decimal
from pathlib import Path

root=Path(__file__).resolve().parent
frames=[]
for line in (root/'target/rk3588_can.log').read_text().splitlines():
    fields=line.split()
    frames.append((Decimal(fields[0].strip('()')),int(fields[2],16),bytes.fromhex(''.join(fields[4:]))))
events=[json.loads(line) for line in (root/'jcan_session.jsonl').read_text().splitlines()]
assert all(event.get('ok') is not False and not event.get('warnings') and not event.get('error') for event in events)
assert events[-1]=={'data':{'stopped':True},'id':99,'ok':True,'op':'shutdown'}
raw=[event for event in events if event.get('event')=='frame']
assert all(not any(event.get(key,False) for key in ('extended','fd','brs','remote')) for event in raw)
wire=[(event['can_id'],bytes.fromhex(event['data_hex'])) for event in raw]
target=[(ident,data) for _,ident,data in frames]
assert [(i,d) for i,d in wire if i!=0x181]==[(i,d) for i,d in target if i!=0x181]
cursor=0
extra=[]
for item in wire:
    if cursor<len(target) and item==target[cursor]:
        cursor+=1
    else:
        extra.append(item)
assert cursor==len(target)
assert extra and all(ident==0x181 for ident,_ in extra)
assert len(wire)<=100000 and len(target)<=100000
assert not any(ident&0x20000000 for ident,_ in wire)
assert all(data[4:6]==bytes(2) for ident,data in wire if ident==0x181)
coordinator=json.loads((root/'coordinator_result.json').read_text())
assert coordinator['error'] is None and coordinator['nonzero_requests']==1
assert coordinator['cleanup_disable_requests']==1
wrapper=json.loads((root/'target/wrapper_result.json').read_text())
assert wrapper=={'executor_started':True,'wrapper_exit':0,'capture_stopped':True,'executor_stopped':True}
assert (root/'target/executor.rc').read_text().strip()=='0'
assert (root/'target/capture.rc').read_text().strip()=='0'
assert (root/'target/capture.stderr').read_text()=='can0: interface down'+chr(10)
assert 'qualification_complete node=1 operation=13' in (root/'target/executor.log').read_text()
event=json.loads((root/'target/privileged_event.json').read_text())
assert event['state']=='up' and event['up_exit']==0
down=Decimal(event['down_before_ns'])/1000000000
up=Decimal(event['up_before_ns'])/1000000000
up_complete=Decimal(event['up_after_ns'])/1000000000
nonzero=[t for t,i,d in frames if i==0x601 and d==bytes.fromhex('23ff600300000500')]
assert len(nonzero)==1 and nonzero[0]<down<up
assert not any(i in (0,0x601) for t,i,d in frames if down<t<up)
resumed=[(t,i,d) for t,i,d in frames if t>=up]
first_request=next((t,i,d) for t,i,d in resumed if i in (0,0x601))
assert first_request[1:]==(0x601,bytes.fromhex('23ff600300000000'))
later=[(t,i,d) for t,i,d in frames if t>nonzero[0]]
assert not any(i==0 and d==bytes([1,1]) for t,i,d in later)
assert not any(i==0x601 and d[:4]==bytes.fromhex('2b406000') and d[4] in (7,15) for t,i,d in later)
preop=next(t for t,i,d in resumed if i==0 and d==bytes([0x80,1]))
disabled=next(t for t,i,d in resumed if i==0x581 and d==bytes.fromhex('4341600060146014'))
final_zero=[]
for sub in (1,2,3):
    final_zero.append(max(t for t,i,d in resumed if i==0x581 and d==bytes([0x43,0x6c,0x60,sub,0,0,0,0])))
assert all(t>disabled>preop for t in final_zero)
for index in (0x2000,0x1017):
    assert any(t>max(final_zero) and i==0x581 and d==bytes([0x4b,index&255,index>>8,0,0,0,0,0]) for t,i,d in resumed)
assert json.loads((root/'jcan_config_pre.json').read_text())==json.loads((root/'jcan_config_post.json').read_text())
post=json.loads((root/'target/can_postflight.json').read_text())[0]
info=post['linkinfo']['info_data']
assert 'UP' in post['flags'] and info['state']=='ERROR-ACTIVE' and info['bittiming']['bitrate']==500000
assert all(value==0 for value in info['berr_counter'].values())
assert all(post['stats64'][direction][key]==0 for direction in ('rx','tx') for key in ('errors','dropped'))
operator=json.loads((root/'operator_observation.json').read_text())
assert operator['accepted']
result={'status':'PASS_BOUNDED_INTERFACE_LOSS','target_frames':len(target),'jcan_frames':len(wire),'target_frames_matched_in_order':cursor,'all_non_tpdo1_frames_identical':True,'extra_jcan_tpdo1_frames':len(extra),'extra_payloads':{data.hex():count for (ident,data),count in Counter(extra).items()},'target_to_interface_down_ms':float((down-nonzero[0])*1000),'interface_hold_ms':(event['up_before_ns']-event['down_after_ns'])/1000000,'up_start_to_first_zero_request_ms':float((first_request[0]-up)*1000),'up_complete_to_first_zero_request_ms':float((first_request[0]-up_complete)*1000),'up_start_to_final_zero_sdo_ms':float((max(final_zero)-up)*1000),'final_status':'0x1460/0x1460','fresh_three_zero_sdo_views':True,'first_resumed_request_packed_zero':True,'no_reenable':True,'watchdog_and_heartbeat_baseline_zero_verified':True,'cleanup_verified':True,'operator_accepted':True,'capture_overflow':False,'reopen_buffer_log_lines':(root/'target/executor.log').read_text().count('RX buffer set to'),'limitations':['JCAN timestamps are zero; extra TPDO timing and physical retransmission cause cannot be established from this trace.','RK3588 cannot capture the disconnected interval; final sampled speed proves recovery zero, not continuous zero or exact stopping time during loss.','Repeated receive-buffer initialization logs during reopen remain a diagnostic rate issue.'],'retry':False}
(root/'physical_result.json').write_text(json.dumps(result,indent=2)+chr(10))
print(json.dumps(result,indent=2))
