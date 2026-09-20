"""Check fixed motion bounds, lease fragmentation, current cues and the coordinated input path offline."""
import ast
import importlib.util
import json
import hashlib
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parent

def load(name):
    """Load one side-effect-free runner module without executing its main entry point."""
    spec = importlib.util.spec_from_file_location(name, BASE / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

target, host = load('target_once'), load('start_once')
auth = json.loads((BASE/'authorization.json').read_text())
assert auth['arguments'] == target.ARGS and auth['artifact_sha256'] == target.SHA == host.SHA
assert target.ARGS[5:] == ['60000','--dual-positive','--zero-feedback-tenths-rpm','20','--motion-window-ms','3000','--right-sign','-1']
assert target.consume_lease(b'',b'LE') == (b'LE',False)
assert target.consume_lease(b'LE',b'ASE\nLEASE\n') == (b'',True)
for bad in (b'',b'ABORT\n',b'LEASE\nBAD',b'L'*4097):
    try: target.consume_lease(b'',bad)
    except AssertionError: continue
    raise AssertionError('invalid lease accepted')
assert host.current_prompt('')[0]=='WAIT'
assert host.current_prompt('CONTROL_READY:')[0]=='CONTROL_READY'
assert host.current_prompt('CONTROL_READY:\nMOTION_READY:')[0]=='MOTION_READY'
for terminal in ('APPLICATION_STOPPING:', 'APPLICATION_EXIT:', 'WINDOW_REMAINING: 0'):
    assert host.current_prompt('MOTION_READY:\n'+terminal+'\n')[0]=='STOP'
sys.path.insert(0,str(Path('scripts/test').resolve()))
from analyze_control_hil_trace import candidate
path=[candidate(1000,raw)[2:] for raw in range(993,1801)]
assert all(0 <= left == right <= 5 for left,right in path) and path[-1]==(5,5)
assert (path[-1][0], -path[-1][1]) == (5,-5), 'wire polarity'
assert candidate(1800,1800)[2:]==(5,0), 'steering deviation must remain outside equal-wheel profile'
for f in BASE.rglob('*.py'): ast.parse(f.read_text())
inputs=json.loads(Path('docs/verification/evidence/g3_polarity_fix_20260918/compiled-inputs.json').read_text())['sha256']
assert all(hashlib.sha256(Path(f).read_bytes()).hexdigest()==h for f,h in inputs.items())
assert hashlib.sha256(Path('out/build/cross/g3-polarity-v1-runtime/tools/control_hil/robot-control-hil').read_bytes()).hexdigest()==target.SHA
assert not (BASE/'CONSUMED').exists()
print('PASS: dual-positive3s/5rpm bounds, lease gate, stale-cue rejection,808 forward-only input points,123 source inputs and artifact identity; no hardware access')

sample='event=control elapsed_ms=100 left_tenths_rpm=30 right_tenths_rpm=30 x=0'
assert target.motion_cue_due(sample,1.0,None)==(1.0,True)
assert target.motion_cue_due(sample.replace('left_tenths_rpm=30','left_tenths_rpm=0'),1.0,None)==(None,False)
assert target.motion_cue_due(sample+' event=stop_cause',1.0,None)==(None,False)
assert host.current_prompt('MOTION_READY: DISCONNECT_NOW:')[0]=='DISCONNECT_NOW'
assert host.current_prompt('MOTION_READY: DISCONNECT_NOW: APPLICATION_STOPPING:')[0]=='STOP'
print('PASS: signal-disconnect cue requires dual movement, stop display priority')
