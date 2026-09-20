from pathlib import Path
import hashlib,json,sys,tarfile,shutil,struct
root=Path('/home/gtc/Desktop/workspace/Linux_PROJ/robot_control')
sys.path[:0]=[str(root/'scripts/test'),str(root/'scripts/hil')]
from analyze_control_soak import analyze,packets
from phase6_zero_motion_soak import parse_capture,health_failures
trial=Path(sys.argv[1]);name=trial.name;duration=int(sys.argv[2])
work=trial/'unpacked'
work.mkdir(exist_ok=True)
with tarfile.open(trial/'target-result.tar.gz') as archive:
 for member in archive.getmembers():
  path=Path(member.name)
  assert path.parts[0]==name and '..' not in path.parts and not path.is_absolute()
  if member.isdir():continue
  assert member.isfile() and len(path.parts)==2
  with archive.extractfile(member) as source,(work/path.name).open('wb') as dest: shutil.copyfileobj(source,dest)
manifest=json.loads((work/'manifest.json').read_text())
for name,expected in manifest.items():
 with (work/name).open('rb') as stream: assert hashlib.file_digest(stream,'sha256').hexdigest()==expected,name
result=json.loads((work/'result.json').read_text())
assert result['status']=='AWAITING_OFFLINE_AUDIT' and result['process_rc']==result['collector_rc']==0 and not result['lease_error']
assert not result['forced_kill']
assert json.loads((trial/'result.json').read_text())['status']=='AWAITING_DUAL_CAPTURE_AUDIT'
software=analyze(work/'trace.bin',duration)
config=json.loads((work/'authorization.json').read_text())
assert config['duration_s']==duration
for row in packets(work/'trace.bin'):
 if row[1]==7:
  cycles,missed,late,cycle=row[3:7]
  assert late<=config['maximum_lateness_us'] and cycle<=config['maximum_cycle_us']
  assert missed<=max(config['missed_startup_allowance'],cycles*config['missed_fraction'])
resource_rows=0
rss_min=rss_max=None
cpu_max=0
with (work/'resources.jsonl').open() as stream:
 for line in stream:
  sample=json.loads(line);resource_rows+=1
  assert sample['rss_kib']<=config['maximum_rss_kib'] and sample['threads']<=config['maximum_threads']
  assert sample['fds']<=config['maximum_fds'] and sample['free_bytes']>=config['reserve_free_bytes']
  assert not sample['lease_error']
  if sample['control_ready']:
   rss_min=sample['rss_kib'] if rss_min is None else min(rss_min,sample['rss_kib'])
   rss_max=sample['rss_kib'] if rss_max is None else max(rss_max,sample['rss_kib'])
   cpu_max=max(cpu_max,sample['cpu_percent'] or 0)
assert resource_rows>=duration*0.9

wire=parse_capture((work/'candump.log').read_text())
assert wire and not (work/'candump.stderr').read_text()
assert all(ident<0x800 for _,ident,_ in wire)
target=[(ident,payload) for _,ident,payload in wire]
first,last,written={},{},set()
pending=None
writes=0
for ident,payload in target:
 if ident==0x181:
  assert len(payload)==8 and all(-20<=v<=20 for v in struct.unpack('<hh',payload[4:])), 'wire feedback outside band'
 if ident==0x201:
  assert len(payload)==6 and payload[2:]==bytes(4), 'wire nonzero target'
 if ident==0x601:
  key=(int.from_bytes(payload[1:3],'little'),payload[3])
  if payload[0]==0x40:pending=key
  else:
   pending=None;writes+=1
   if key[0]!=0x6040:written.add(key)
 elif ident==0x581 and payload[0] in (0x43,0x47,0x4b,0x4f):
  key=(int.from_bytes(payload[1:3],'little'),payload[3]);assert pending==key
  value=int.from_bytes(payload[4:8-((payload[0]>>2)&3)],'little')
  first.setdefault(key,value);last[key]=value;pending=None
  if key[0]==0x606c:
   speeds=struct.unpack('<hh',payload[4:]) if key[1]==3 else struct.unpack('<i',payload[4:])
   assert all(-20<=v<=20 for v in speeds), 'wire SDO feedback outside band'
assert written and all(key in first and first[key]==last[key] for key in written), 'baseline restoration mismatch'

independent=[];connected=0
with (trial/'jcan/session.jsonl').open() as stream:
 for line in stream:
  event=json.loads(line)
  if event.get('event')=='connected':connected+=1
  if event.get('event')=='frame':
   assert not any(event[x] for x in ('extended','fd','remote'))
   independent.append((event['can_id'],bytes.fromhex(event['data_hex'])))
assert connected==1
starts=[i for i,pair in enumerate(independent) if pair==target[0] and independent[i:i+len(target)]==target]
strict_order = target == independent
assert len(target)==len(independent)==518836
assert target[:116748]==independent[:116748] and target[116751:]==independent[116751:]
assert target[116748:116751]==[(0x201,bytes.fromhex('0f0000000000'))]*2+[(0x281,bytes.fromhex('0300000000'))]
assert independent[116748:116751]==[target[116750],target[116748],target[116749]]
for direction in (True,False):
 assert [frame for frame in target if (frame[0] in (0,0x201,0x601))==direction]==[frame for frame in independent if (frame[0] in (0,0x201,0x601))==direction]
for ident in {frame[0] for frame in target}:
 assert [p for i,p in target if i==ident]==[p for i,p in independent if i==ident]
starts=[0]

assert not health_failures(json.loads((work/'can-before.json').read_text()),json.loads((work/'can-after.json').read_text()))
cap=json.loads((trial/'jcan/result.json').read_text())
assert cap['error'] is None and cap['shutdown_ack'] and cap['process_rc']==0
assert json.loads((trial/'jcan/config_pre.json').read_text())==json.loads((trial/'jcan/config_post.json').read_text())
operator=json.loads((trial/'operator-off.json').read_text())
assert operator['accepted_conditions'] and operator['response']=='OFF'
audit=dict(status='PENDING_OPERATOR_ACCEPTANCE',original_strict_order='FAIL',functional_checks='PASS',directional_sequences_equal=True,all_id_sequences_equal=True,local_reorder_indices=[116748,116749,116750],resource_samples=resource_rows,rss_min_kib=rss_min,rss_max_kib=rss_max,cpu_peak_percent=cpu_max,sdo_downloads=writes,restored_objects=len(written),software=software,matched_can_frames=len(target),jcan_frames=len(independent),
           jcan_extra_prefix=starts[0],jcan_extra_suffix=len(independent)-starts[0]-len(target),
           operator=operator,scope='Unloaded continuous real-SBUS zero-target control only; no moving or loaded qualification')
(trial/'reviewed-analysis.json').write_text(json.dumps(audit,indent=2)+'\n')
print(json.dumps(audit,indent=2))
