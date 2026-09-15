"""Verify the actual coordinator frame bound without hardware access."""
import ast
from pathlib import Path
tree = ast.parse((Path(__file__).parent / 'local_trial.py').read_text())
fn = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'validate_frame')
scope = dict(frames=0, downloads=0, uploads=0, nmts=0, nonzero=0, terminal=0,
             recovery=0, nmt_stop=0, startup_recovery=0, cleanup_disable=0, go=True)
exec(compile(ast.Module(body=[fn], type_ignores=[]), 'validator', 'exec'), scope)
frame = {'can_id': 0x181, 'data_hex': '27 14 27 44 00 00 33 00'}
for _ in range(100000):
    scope['validate_frame'](frame)
assert scope['frames'] == 100000
try:
    scope['validate_frame'](frame)
except AssertionError:
    pass
else:
    raise AssertionError('capture bound absent')
print('PASS: 100000 accepted; 100001 rejected; no hardware accessed')
