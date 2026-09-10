#!/usr/bin/env python3
"""Check the capture guard offline without importing its hardware entry point."""
from pathlib import Path
import ast
import json
ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / 'local_trial.py'
NODE = next(n for n in ast.parse(SOURCE.read_text()).body if isinstance(n, ast.FunctionDef) and n.name == 'validate_frame')

def fresh():
    """Compile only the pure frame-validation function with fresh counters."""
    scope = dict(go=True, frames=0, downloads=0, uploads=0, nmts=0, nonzero=0, terminal=0)
    exec(compile(ast.Module(body=[NODE], type_ignores=[]), str(SOURCE), 'exec'), scope)
    return scope

scope = fresh()
prior = ROOT.parent / 'p6_6_20260910_disable_voltage_corrected_trial_1/jcan_session.jsonl'
for line in prior.read_text().splitlines():
    frame = json.loads(line)
    if frame.get('event') != 'frame':
        continue
    if frame['can_id'] == 0x601 and frame['data_hex'] == '2B 40 60 00 00 00 00 00':
        frame['data_hex'] = '2B 40 60 00 02 00 00 00'
    scope['validate_frame'](frame)
assert scope['nonzero'] == 1 and scope['terminal'] == 1
for payload in ('23 FF 60 01 05 00 00 00', '2B 00 20 00 F4 01 00 00', '2B 40 60 00 00 00 00 00'):
    try:
        fresh()['validate_frame']({'can_id': 0x601, 'data_hex': payload})
    except (AssertionError, RuntimeError):
        pass
    else:
        raise AssertionError('unapproved frame accepted')
try:
    scope['validate_frame']({'can_id': 0x601, 'data_hex': '2B 40 60 00 02 00 00 00'})
except AssertionError:
    pass
else:
    raise AssertionError('duplicate terminal accepted')
print('PASS: recorded traffic shape, wrong-axis motion, watchdog write, Disable Voltage and repeated terminal checks')
