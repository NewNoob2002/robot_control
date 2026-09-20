"""Check ground zero-only runner bounds and live recovery cues without hardware."""
import ast
import hashlib
import importlib.util
import json
from pathlib import Path
BASE = Path(__file__).resolve().parent
ROOT = next(p for p in BASE.parents if (p/'scripts/test/analyze_control_hil_trace.py').exists())

def load(name):
    """Import one guarded runner without invoking its hardware main function."""
    spec = importlib.util.spec_from_file_location(name, BASE/(name+'.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

target, host = load('target_once'), load('start_once')
auth=json.loads((BASE/'authorization.json').read_text())
assert target.ARGS == auth['arguments'] and target.SHA == host.SHA == auth['artifact_sha256']
assert target.ARGS[5:] == ['2000','--restore-zero-baseline','--zero-feedback-tenths-rpm','20','--right-sign','-1']
assert target.consume_lease(b'',b'LE') == (b'LE',False)
assert target.consume_lease(b'LE',b'ASE'+bytes([10])) == (b'',True)
for bad in (b'',b'ABORT'+bytes([10]),b'BAD',b'L'*4097):
    try: target.consume_lease(b'',bad)
    except AssertionError: continue
    raise AssertionError('invalid lease accepted')
log=''
assert host.current_prompt(log)[0]=='WAIT'
for name,_ in host.PROMPTS:
    log+=name+': observed'+chr(10)
    assert host.current_prompt(log)[0]==name
for terminal in ('APPLICATION_EXIT:', 'APPLICATION_STOPPING:', 'WINDOW_REMAINING: 0'):
    assert host.current_prompt(log+terminal+chr(10))[0]=='STOP'
for p in BASE.rglob('*.py'): ast.parse(p.read_text())
assert 'wheels_raised' not in (BASE/'start_once.py').read_text()+(BASE/'target_once.py').read_text()
assert "ready['mechanical_restraint']" in (BASE/'target_once.py').read_text()
manifest=json.loads((ROOT/'docs/verification/evidence/g3_differential_preparation_20260920/compiled-inputs.json').read_text())['sha256']
assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in manifest.items())
assert hashlib.sha256((ROOT/'out/build/cross/g3-differential-v1-runtime/tools/control_hil/robot-control-hil').read_bytes()).hexdigest()==target.SHA
assert not (BASE/'CONSUMED').exists()
print('PASS: disabled-only restore/CLI20/right-sign-1, fragmented lease, recovery/stale cues, ground readiness,123 inputs/artifact; no hardware')
