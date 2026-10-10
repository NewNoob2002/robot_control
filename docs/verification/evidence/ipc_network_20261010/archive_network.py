import gzip
import hashlib
import json
from pathlib import Path
import shutil
import struct
from datetime import datetime,timezone

root=Path.cwd()
out=root/'out/verification/ipc-network-diagnosis-20261009'
archive=root/'docs/verification/evidence/ipc_network_20261010'
paths=['app/main.cpp','application/control/linux_loop.cpp','application/control/linux_loop.hpp',
       'service/logging/stderr_sink.cpp','service/logging/stderr_sink_status.h','tests/integration/linux_platform_tests.cpp']
lsp={'source':'lens_diagnostics active lsp, primary server','date_utc':'2026-10-10',
     'individual':{'wait_ms':5000,'paths':paths,'confirmed_clean':6,'findings':0,'timeouts':0},
     'batch':{'wait_ms':10000,'concurrency':2,'confirmed_clean':6,'findings':0,'timeouts':0},
     'historical_timeout':'Not reproducible now; historical scheduler/cache/load cause not established'}
(archive/'LSP.json').write_text(json.dumps(lsp,indent=2)+'\n')

captures={}
for mode in ('default','udp','port-probe'):
    capture=json.loads((out/f'{mode}.capture.json').read_text())
    blob=(out/f'{mode}.pcap').read_bytes()
    assert blob[:4]==b'\xd4\xc3\xb2\xa1' and len(blob)<2*1024*1024
    cursor=24
    packets=[]
    while cursor<len(blob):
        seconds,micros,size,original=struct.unpack_from('<IIII',blob,cursor)
        packet=blob[cursor+16:cursor+16+size]
        assert len(packet)==size==original
        offset=14+(packet[14]&15)*4
        sport,dport,length,_=struct.unpack_from('!HHHH',packet,offset)
        payload=packet[offset+8:offset+length]
        packets.append((sport,dport,payload))
        cursor+=16+size
    assert len(packets)==sum(capture['counts'].values())
    capture['pcap_packets_verified']=len(packets)
    captures[mode]=capture
    if mode=='port-probe':
        assert sum(payload==b'RC_UDP_DIAG SPONTANEOUS' for _,_,payload in packets)==4
        assert sum(payload==b'RC_UDP_DIAG PING' for _,_,payload in packets)==1
        assert sum(payload==b'RC_UDP_DIAG PONG' for _,_,payload in packets)==1

probe=json.loads((out/'port-probe.json').read_text())
assert not probe['spontaneous_delivered'] and probe['related_reply_delivered']
log=(out/'ufw-block-final.log').read_text()
assert '[UFW BLOCK]' in log and 'DPT=53910' in log and 'DPT=54710' in log
for case in ('default','udp'):
    value=json.loads((out/f'{case}-dds.json').read_text())
    assert value['samples_by_server']=={} and value['passed'] is False
positive=json.loads((out/'tcp-dds.json').read_text())
assert positive['passed'] and sum(positive['samples_by_server'].values())==229
assert not json.loads((out/'tcp-postflight.json').read_text())['node_running']

for path in sorted(out.iterdir()):
    if not path.is_file() or path.suffix not in ('.json','.jsonl','.log','.pcap','.xml','.py'):
        continue
    dest=archive/path.name
    compressed=path.suffix in ('.jsonl','.pcap')
    if compressed:
        dest=dest.with_name(dest.name+'.gz')
    assert not dest.exists(),dest
    if compressed:
        dest.write_bytes(gzip.compress(path.read_bytes(),mtime=0))
    else:
        shutil.copy2(path,dest)

result={'schema_version':1,'recorded_utc':datetime.now(timezone.utc).isoformat(),
    'verdict':'HOST_UFW_UDP_BLOCK_CONFIRMED_REMEDIATION_NOT_APPLIED',
    'target':json.loads((out/'PREFLIGHT.json').read_text()),
    'captures':captures,'independent_udp_probe':probe,
    'confirmed_kernel_drop_ports':[53910,54710],
    'current_default_dds':'FAILED_ZERO_SAMPLES','current_explicit_udp':'FAILED_ZERO_SAMPLES',
    'current_tcp_control':positive,'cpp_lsp':lsp,
    'rule_proposal':json.loads((out/'UDP_RULE_PROPOSAL.json').read_text()),
    'code_changes_required_for_this_block':False,'global_firewall_changed':False,
    'physical_can_uart_used':False,'production_changed':False,
    'postflight':json.loads((out/'tcp-postflight.json').read_text()),
    'acceptance_limits':['UDP has not been repaired; scoped firewall action and post-change retest require authorization',
        'One fixed-profile firewall proposal is not default multicast or multi-participant ROS acceptance',
        'Current topology differs from the old routed/NAT network; not every old failure is uniquely attributed',
        'Historical LSP timeout cause not established, current active six-file checks all pass',
        'Synthetic fixture status only; source age across hosts and hardware/motion remain unverified'],
    'auxiliary_probe_corrections':['Native /usr/bin/python3 used for installed UFW instead of isolated Pi Python',
        'UFWParserResponse uses data[rule]; allow/rollback parse succeeded without backend construction']}
(archive/'RESULT.json').write_text(json.dumps(result,indent=2)+'\n')
files=['scripts/test/test_ros2_ipc_remote_status.py','ros2/robot_control_ipc_node.py']+paths
(archive/'FILES.json').write_text(json.dumps({name:hashlib.sha256((root/name).read_bytes()).hexdigest()
    for name in files},indent=2,sort_keys=True)+'\n')
checksums=''.join(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(archive)}\n'
    for path in sorted(archive.iterdir()) if path.is_file() and path.name!='SHA256SUMS')
(archive/'SHA256SUMS').write_text(checksums)
print('DIAGNOSIS_ARCHIVE_PASS',len(checksums.splitlines()),'files, 4 blocked spontaneous packets and 1 delivered related reply')
