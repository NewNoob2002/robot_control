"""Offline checks for the one-shot identity, zero command and fragmented lease gate."""
import ast
import importlib.util
import json
from pathlib import Path

BASE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('target', BASE / 'target_once.py')
target = importlib.util.module_from_spec(spec)
spec.loader.exec_module(target)
auth = json.loads((BASE / 'authorization.json').read_text())
assert auth['arguments'] == target.ARGS and auth['artifact_sha256'] == target.SHA
assert target.ARGS[5:8] == ['20000', '--zero-only', '--zero-feedback-tenths-rpm']
assert target.ARGS[-3:] == ['20','--right-sign','-1'] and auth['attempts'] == 1
assert target.consume_lease(b'', b'LE') == (b'LE', False)
assert target.consume_lease(b'LE', b'ASE\nLEASE\nL') == (b'L', True)
for data in (b'', b'ABORT\n', b'LEASE\nBAD', b'L'*4097):
    try:
        target.consume_lease(b'', data)
    except AssertionError:
        continue
    raise AssertionError('invalid lease accepted')
for path in BASE.rglob('*.py'):
    ast.parse(path.read_text())
assert not (BASE / 'CONSUMED').exists()
print('PASS: fixed zero-only identity/bounds, fragmented/invalid/EOF lease checks, Python syntax; no hardware access')

assert target.enabled_zero_logged('event=control armed=1 word=15 left_tenths_rpm=0 right_tenths_rpm=0')
assert not target.enabled_zero_logged('event=control armed=0 word=15 left_tenths_rpm=0 right_tenths_rpm=0')
assert not target.enabled_zero_logged('event=control armed=1 word=15 left_tenths_rpm=1 right_tenths_rpm=0')
assert not target.enabled_zero_logged('')
print('PASS: explicit enabled-zero operator cue and rejected nonzero/unarmed status')
