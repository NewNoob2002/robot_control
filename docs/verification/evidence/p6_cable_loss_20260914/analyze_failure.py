"""Preserve the failed cable-loss trial's observations without hardware access or success assumptions."""
import json
from collections import Counter
from decimal import Decimal
from pathlib import Path

root=Path(__file__).resolve().parent
frames=[]
for line in (root/'target/rk3588_can.log').read_text().splitlines():
    if not line.lstrip().startswith('('):
        continue
    fields=line.split()
    payload=bytes.fromhex(''.join(fields[4:12]))
    frames.append((Decimal(fields[0].strip('()')),int(fields[2],16),payload))
events=[json.loads(line) for line in (root/'jcan_session.jsonl').read_text().splitlines()]
wire=[(event['can_id'],bytes.fromhex(event['data_hex'])) for event in events if event.get('event')=='frame']
normal=[(i,d) for _,i,d in frames if not i&0x20000000]
error_frames=[{'timestamp':str(t),'can_id':hex(i),'data_hex':d.hex()} for t,i,d in frames if i&0x20000000]
matched=0
extra=[]
for item in wire:
    if matched<len(normal) and item==normal[matched]:
        matched+=1
    else:
        extra.append(item)
nonzero=[t for t,i,d in frames if i==0x601 and d==bytes.fromhex('23ff600300000500')]
assert len(nonzero)==1
after=[(t,i,d) for t,i,d in frames if t>nonzero[0]]
zeros=[str(t) for t,i,d in after if i==0x601 and d==bytes.fromhex('23ff600300000000')]
log=(root/'target/executor.log').read_text()
assert 'cleanup_failed=' in log and 'operator_power_cut_required' in log
post=json.loads((root/'target/can_postflight.json').read_text())[0]
post['linkinfo']['info_data']['info_xstats']=post['linkinfo']['info_xstats']
result={'date':'2026-09-14','status':'FAIL_CABLE_LOSS_CLEANUP_UNVERIFIED','target_frames':len(frames),'target_normal_frames':len(normal),'jcan_frames':len(wire),'target_normal_frames_matched_in_order':matched,'extra_jcan_frames':len(extra),'extra_payload_counts':{hex(i)+'#'+d.hex():n for (i,d),n in Counter(extra).items()},'can_error_frames':error_frames,'first_error_after_nonzero_ms':float((next(t for t,i,d in frames if i&0x20000000)-nonzero[0])*1000),'zero_target_requests_seen_after_motion':zeros,'permission_denied_log_lines':log.count('Permission denied'),'postflight_can_state':post['linkinfo']['info_data']['state'],'postflight_xstats':post['linkinfo']['info_data']['info_xstats'],'cleanup_verified':False,'final_zero_speed_verified':False,'temporary_settings_restored':False,'operator_confirmation_pending':not (root/'operator_observation.json').exists(),'executor_and_capture_stopped':json.loads((root/'target/wrapper_result.json').read_text()),'jcan_shutdown_confirmed':events[-1].get('data',{}).get('stopped') is True,'retry':False,'later_physical_tests_blocked':True,'limits':['Receive error/drop counters and instantaneous error counters do not override observed controller error frames or ERROR-PASSIVE status.','JCAN timestamps are zero; repeated payload timing and stopping during disconnection cannot be inferred.','Permission-denied logs alone do not identify which exact frame or authorization condition was rejected.']}
result['operator_power_state']=json.loads((root/'operator_observation.json').read_text()).get('latest_power_state_operator_reported','unknown')
(root/'failure_analysis.json').write_text(json.dumps(result,indent=2)+chr(10))
print(json.dumps(result,indent=2))
