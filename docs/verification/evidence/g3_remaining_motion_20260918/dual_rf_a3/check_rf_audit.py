"""Exercise RF causality with a minimal trace and reject unrelated/late stops offline."""
import copy
import importlib.util
from pathlib import Path

BASE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('rf_audit', BASE/'analyze-rf.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
log = 'phase=control ok=0 operation=control_hil_motion_envelope source_fault=3'
moving = bytes.fromhex('0f000500fbff')
zero = bytes.fromhex('020000000000')
status = bytes.fromhex('270027003200ceff')
cycle = [0]*16
cycle[15] = 4
trace = dict(discontinuities=0, tx=[(1000000000,0x201,moving),(1500010000,0x201,zero)],
             rx=[(1100000000,0x181,status,16,0),(1200000000,0x181,status,16,0)],
             frames=[dict(ns=1400000000,flags=0),dict(ns=1500000000,flags=4)],
             cycles=[(1510000000,cycle)],stops=[dict(cause=5)])
assert audit.check_loss(log,trace)['loss_to_zero_us'] == 10
failsafe = copy.deepcopy(trace)
failsafe['frames'][-1]['flags'] = 8
assert audit.check_loss(log.replace('fault=3','fault=4'),failsafe)['kind']=='failsafe'

for case in ('no_flags','late_loss','pre_motion_loss','cutoff','restart','discontinuity','x1','rearm','no_dual_motion','excess_latency'):
    bad = copy.deepcopy(trace)
    if case == 'no_flags': bad['frames'][-1]['flags'] = 0
    if case == 'late_loss': bad['frames'][-1]['ns'] = 1600000000
    if case == 'pre_motion_loss': bad['frames'][0] = dict(ns=900000000,flags=4)
    if case == 'cutoff': bad['tx'][-1] = (3950000000,0x201,zero)
    if case == 'restart': bad['tx'].append((1600000000,0x201,moving))
    if case == 'discontinuity': bad['discontinuities'] = 1
    if case == 'x1': bad['rx'][0] = (1100000000,0x181,bytes.fromhex('278027003200ceff'),16,0)
    if case == 'rearm': bad['cycles'].append((1600000000,[0]*14+[1,4]))
    if case == 'no_dual_motion': bad['rx'] = bad['rx'][:1]
    if case == 'excess_latency': bad['tx'][-1] = (1700000000,0x201,zero)
    try:
        audit.check_loss(log,bad)
    except AssertionError:
        continue
    raise AssertionError('accepted '+case)
try:
    audit.check_loss(log.replace('fault=3','fault=5'),trace)
except AssertionError:
    pass
else:
    raise AssertionError('timeout-only accepted as RF evidence')
print('PASS: frame_lost/failsafe causality and11 invalid-stimulus/timing/rearm cases; no hardware access')
