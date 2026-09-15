"""Verify one zero-target recovery using paired physical bus captures."""
import json
from pathlib import Path
out=Path(__file__).resolve().parent
frames=[]
for line in (out/'target/rk3588_can.log').read_text().splitlines():
    parts=line.split()
    data=bytes.fromhex(''.join(parts[4:]))
    assert len(data)==int(parts[3].strip('[]'))
    frames.append((float(parts[0].strip('()')),int(parts[2],16),data))
events=[json.loads(line) for line in (out/'jcan_session.jsonl').read_text().splitlines()]
assert all(e.get('ok') is not False and not e.get('warnings') and not e.get('error') for e in events)
observed=[(e['can_id'],bytes.fromhex(e['data_hex'])) for e in events if e.get('event')=='frame']
assert [(i,d) for t,i,d in frames]==observed
assert len(frames)==122
pending=None
reads={}
transactions=0
for t,i,d in frames:
    if i==0x601:
        assert pending is None
        pending=d
        if d[1:3]==bytes.fromhex('FF60') and d[0]!=0x40:
            assert d[3] in (1,2) and d[4:]==bytes(4)
    elif i==0x581:
        assert pending is not None and pending[1:4]==d[1:4] and d[0]!=0x80
        transactions+=1
        if pending[0]==0x40:
            key=(int.from_bytes(d[1:3],'little'),d[3])
            reads[key]=int.from_bytes(d[4:],'little')
            if key[0] in (0x606C,0x60FF,0x603F): assert reads[key]==0
        else: assert d[0]==0x60
        pending=None
assert pending is None
assert all(reads[k]==0 for k in [(0x60FF,1),(0x60FF,2),(0x606C,1),(0x606C,2),(0x606C,3),(0x1017,0)])
assert reads[(0x6061,0)]==3 and reads[(0x1800,5)]==100
controls=[(t,int.from_bytes(d[4:],'little')) for t,i,d in frames if i==0x601 and d[:4]==bytes.fromhex('2B406000')]
assert [v for t,v in controls]==[0,6,7,15,6]
tpdos=[(t,d) for t,i,d in frames if i==0x181]
assert tpdos and all(len(d)==8 and d[4:]==bytes(4) for t,d in tpdos)
assert any(t<controls[0][0] and d[:4]==bytes.fromhex('07140714') for t,d in tpdos)
transitions=[]
for index,((at,command),status) in enumerate(zip(controls,[0x1440,0x1421,0x1423,0x1427,0x1421])):
    end=controls[index+1][0] if index+1<len(controls) else float('inf')
    seen=next(t for t,d in tpdos if at<t<end and d[:4]==status.to_bytes(2,'little')*2)
    assert (seen-at)*1000<2000
    transitions.append({'controlword':hex(command),'dual_statusword':hex(status),'command_to_tpdo_ms':round((seen-at)*1000,3)})
assert [(i,d) for t,i,d in frames if i==0]==[(0,bytes([1,1])),(0,bytes([128,1]))]
assert [d for t,i,d in frames if i==0x701][-1]==bytes([127])
pre=json.loads((out/'target/can_preflight.json').read_text())[0]
post=json.loads((out/'target/can_postflight.json').read_text())[0]
assert post['linkinfo']['info_data']['state']=='ERROR-ACTIVE'
assert all(v==0 for v in post['linkinfo']['info_data'].get('berr_counter',{}).values())
for direction in ('rx','tx'):
    assert all(post['stats64'][direction][key]==0 for key in ('errors','dropped'))
    selected=[d for t,i,d in frames if (i in (0,0x601))==(direction=='tx')]
    assert post['stats64'][direction]['packets']-pre['stats64'][direction]['packets']==len(selected)
    assert post['stats64'][direction]['bytes']-pre['stats64'][direction]['bytes']==sum(map(len,selected))
coordinator=json.loads((out/'coordinator_result.json').read_text())
wrapper=json.loads((out/'target/wrapper_result.json').read_text())
assert coordinator['error'] is None and coordinator['disable_voltage_recovery_requests']==1
assert wrapper['wrapper_exit']==0 and wrapper['capture_stopped'] and wrapper['executor_stopped']
assert (out/'target/executor.rc').read_text()=='0' and (out/'target/capture.rc').read_text()=='0'
assert json.loads((out/'jcan_config_pre.json').read_text())['data']==json.loads((out/'jcan_config_post.json').read_text())['data']
result={'protocol_and_cleanup_passed':True,'attempts':1,'elf_sha256':'31d2d0848e9330f6f910e642d47bccb6552ca112cefa36214defa646c524b7e3','frames':len(frames),'capture_pairs_equal':True,'sdo_transactions':transactions,'transitions':transitions,'nonzero_targets':0,'all_speed_readbacks_zero':True,'all_tpdo_speeds_zero':True,'heartbeat_restored':0,'can_errors_drops':0,'final_status_raw':tpdos[-1][1][:4].hex(),'physical_observation':'pending','nmt_stop_executed':False}
(out/'analysis.json').write_text(json.dumps(result,indent=2)+'\n')
(out/'bus_validation.json').write_text(json.dumps(result,indent=2)+'\n')
(out/'test_result.json').write_text(json.dumps({'status':'protocol_and_cleanup_pass_operator_pending','command':['robot-control-zlac-qualification','--interface','can0','--zero-sequence'],'target':'RK3588/node1','requirements':['single Quick Stop zero recovery','no nonzero targets','dual disabled feedback before startup','verified zero cleanup'],'analysis':'analysis.json','passed':1,'failed':0,'skipped':0},indent=2)+'\n')
print(json.dumps(result,indent=2))
