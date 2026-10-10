import gzip
import hashlib
import json
import shutil
import struct
from datetime import datetime,timezone
from pathlib import Path

root=Path.cwd()
out=root/'out/verification/ipc-udp-allow-20261010'
archive=root/'docs/verification/evidence/ipc_udp_allow_20261010'
positive=json.loads((out/'udp-dds.json').read_text())
assert positive['passed'] and sum(positive['samples_by_server'].values())==388
probe=json.loads((out/'port-probe.json').read_text())
assert sorted(item['port'] for item in probe['spontaneous_delivered'])==[54710,54710,54711,54711]
negative=json.loads((out/'default-dds.json').read_text())
assert not negative['passed'] and not negative['samples_by_server']
assert (out/'ufw-apply.log').read_text().strip()=='Rule added'
cleanup=json.loads((out/'CLEANUP.json').read_text())
assert cleanup['passed']
captures={}
for case in ('udp','default','port-probe'):
    blob=(out/f'{case}.pcap').read_bytes()
    assert blob[:4]==b'\xd4\xc3\xb2\xa1' and len(blob)<2*1024*1024
    cursor=24
    packets=[]
    while cursor<len(blob):
        seconds,micros,size,original=struct.unpack_from('<IIII',blob,cursor)
        packet=blob[cursor+16:cursor+16+size]
        assert len(packet)==size==original
        offset=14+(packet[14]&15)*4
        sport,dport,length,_=struct.unpack_from('!HHHH',packet,offset)
        packets.append((dport,packet[offset+8:offset+length]))
        cursor+=16+size
    value=json.loads((out/f'{case}.capture.json').read_text())
    assert len(packets)==sum(value['counts'].values())
    value['pcap_packets_verified']=len(packets)
    captures[case]=value
    if case=='port-probe':
        assert sum(data==b'RC_UDP_DIAG SPONTANEOUS' for _,data in packets)==6
        assert sum(port==54712 for port,_ in packets)==2

rule=json.loads((root/'docs/verification/evidence/ipc_network_20261010/UDP_RULE_PROPOSAL.json').read_text())
rule.update({'applied':True,'newly_added':True,'apply_output':'Rule added',
             'authorization_source':'User: 授权我添加这条限定规则，并复测 UDP 通信',
             'retained_on_success':True,'rollback_performed':False,
             'rollback_safety':'Remove only this actually newly added exact rule; do not use a changing rule number',
             'authorization_granted_for_this_rule':True})
(archive/'RULE.json').write_text(json.dumps(rule,indent=2)+'\n')
for path in sorted(out.iterdir()):
    if not path.is_file() or path.suffix not in ('.json','.jsonl','.log','.pcap','.xml','.py'):
        continue
    dest=archive/path.name
    if path.suffix in ('.pcap','.jsonl'):
        dest=dest.with_name(dest.name+'.gz')
        assert not dest.exists(),dest
        dest.write_bytes(gzip.compress(path.read_bytes(),mtime=0))
    else:
        assert not dest.exists(),dest
        shutil.copy2(path,dest)
result={'schema_version':1,'recorded_utc':datetime.now(timezone.utc).isoformat(),
        'verdict':'PASS_AUTHORIZED_SCOPED_UDP_DEVICE_FREE_DIAGNOSTICS',
        'authorization':json.loads((out/'PREFLIGHT.json').read_text()),'rule':rule,
        'explicit_udp_dds':positive,'independent_socket_probe':probe,
        'neighbor_port_negative_check':{'port':54712,'arrived_at_interface':2,'delivered_to_bound_socket':0,'passed':True},
        'default_dds_negative_check':{'expected_denial':True,'raw_observer_verdict':negative,'host_exit_code':1,'passed':True},
        'captures':captures,'cleanup':cleanup,'host_firewall_changed':True,
        'board_system_network_changed':False,'physical_can_uart_used':False,'production_changed':False,
        'limitations':['Fixed explicit UDP profiles and one host participant only; default multicast remains unapproved',
                       'Freshness is receipt-only; source-state age across hosts and remote probes unverified',
                       'Synthetic fixture status only; hardware CAN/UART/drive/motion not exercised',
                       'Server exit/new-instance recovery verified; no new physical link interruption acceptance',
                       'No source behavior change, commit or remote CI run in this follow-up']}
(archive/'RESULT.json').write_text(json.dumps(result,indent=2)+'\n')
files=['scripts/test/test_ros2_ipc_remote_status.py','ros2/robot_control_ipc_node.py']
(archive/'FILES.json').write_text(json.dumps({name:hashlib.sha256((root/name).read_bytes()).hexdigest()
    for name in files},indent=2,sort_keys=True)+'\n')
checksums=''.join(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(archive)}\n'
    for path in sorted(archive.iterdir()) if path.is_file() and path.name!='SHA256SUMS')
(archive/'SHA256SUMS').write_text(checksums)
print('UDP_ACCEPTANCE_ARCHIVE_PASS',len(checksums.splitlines()),'files; 388 healthy samples, 4 allowed/2 denied socket packets')
