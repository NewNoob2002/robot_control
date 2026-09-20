"""Audit the incomplete RF attempt without claiming any motion or RF stimulus."""
import importlib.util
import json
from pathlib import Path

BASE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('protocol', BASE.parent/'dual_x1_a1/analyze-x1.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
log = (BASE/'physical-once/application.log').read_text()
result = audit.analyze(log)
summary = audit.fields(next(line for line in log.splitlines() if line.startswith('event=summary ')))
assert summary['enabled_samples']=='0' and summary['restore_ok']=='1' and summary['primary_ok']=='0'
assert result['right_sign']==-1 and not result['frames'] and not result['discontinuities']
rows = [audit.fields(line) for line in log.splitlines() if line.startswith('event=trace_row ')]
batches = [[int(v) for v in row['fields'].split(',')] for row in rows if row['kind']=='0']
assert batches and all(d[2:5]==[0,0,0] for d in batches)
targets = [p for _,i,p in result['tx'] if i==0x201]
assert targets and all(p[2:]==bytes(4) for p in targets)
assert all(not d[14] and d[9:13]==[0,0,0,0] for _,d in result['cycles'])
assert not result['feedback_bad']
assert all(abs(int.from_bytes(p[o:o+2],'little',signed=True))<=20
           for _,i,p,_,_ in result['rx'] if i==0x181 for o in (4,6))
wire = audit.parse_capture((BASE/'physical-once/candump.log').read_text())
independent = []
for line in (BASE/'jcan/session.jsonl').read_text().splitlines():
    event = json.loads(line)
    if event.get('event')=='frame':
        assert not any(event[k] for k in ('brs','extended','fd','remote'))
        independent.append((event['can_id'],bytes.fromhex(event['data_hex'])))
protocol = audit.audit_protocol(result,wire,independent,'dual-positive',20)
protocol['scope'] = 'Zero-output and restoration only; no motion/RF acceptance'
before = json.loads((BASE/'physical-once/can-before.json').read_text())
after = json.loads((BASE/'physical-once/can-final.json').read_text())
assert before['linkinfo']==after['linkinfo']
for side in ('rx','tx'):
    for key,value in before['stats64'][side].items():
        if key not in ('bytes','packets'):
            assert after['stats64'][side][key]==value
host = json.loads((BASE/'result.json').read_text())
assert not host['passed'] and host['capture_clean']
report = dict(status='INCOMPLETE_NO_UART_BYTES',summary=summary,read_batches=len(batches),
              raw_bytes=0,sbus_frames=0,zero_rpdos=len(targets),motion_occurred=False,
              protocol=protocol,can_counters_unchanged=True,
              source_fault='14 shutdown remains from create-time authority revocation; no fresh input',
              physical_cause='Operator confirms SBUS data line not reconnected after previous trial',
              original_run_preserved=True)
(BASE/'analysis.json').write_text(json.dumps(report,indent=2)+chr(10))
print(json.dumps(report,indent=2))
