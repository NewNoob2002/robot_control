"""Offline regression checks; no device, network or capture subprocess is permitted."""
from pathlib import Path
import ast
import json
import runpy
import tempfile
from unittest.mock import patch
from operator_console import current_prompt, PROMPTS

BASE = Path(__file__).resolve().parent


def main():
    """Check live/expired phase selection, archived failure and readiness rejection."""
    for path in BASE.glob('*.py'):
        ast.parse(path.read_text(), filename=str(path))
    log = ''
    for name, _ in PROMPTS:
        log += name + ': instruction\nWINDOW_REMAINING: 20\n'
        assert current_prompt(log)[0] == name
    for marker in ('APPLICATION_EXIT: stopped', 'APPLICATION_STOPPING: cleanup', 'WINDOW_REMAINING: 0'):
        assert current_prompt(log + marker + '\nFAULT_READY: stale\n')[0] == 'STOP'
    assert current_prompt('')[0] == 'WAIT'
    assert current_prompt('CONTROL_READY: go\nWINDOW_REMAINING: 42\n')[2] == 42
    assert current_prompt((BASE.parent / 'zero_x1_a1/recovery-execution.log').read_text())[0] == 'STOP'
    runner = runpy.run_path(str(BASE / 'run-recovery.py'))
    with tempfile.TemporaryDirectory() as directory:
        runner['main'].__globals__['BASE'] = Path(directory)
        with patch('subprocess.Popen', side_effect=AssertionError('Subprocess before readiness')):
            try:
                runner['main']()
                raise AssertionError('Missing readiness accepted')
            except FileNotFoundError:
                pass
    causal = runpy.run_path(str(BASE / 'analyze-sbus.py'))['check']
    from copy import deepcopy
    moving = bytes.fromhex('0f0000000500')
    zero = bytes.fromhex('060000000000')
    speed = bytes.fromhex('2714271400003200')
    data = [0]*16
    data[4], data[15] = 200000000, 4
    trace = dict(tx=[(100000000,0x201,moving),(210000000,0x201,zero)],
                 rx=[(150000000,0x181,speed,0,0)],
                 frames=[dict(ns=90000000,flags=0),dict(ns=200000000,flags=4)],
                 cycles=[(211000000,data)],stops=[dict(cause=5)])
    assert causal(trace,3)['loss_to_zero_ms'] == 10
    failsafe=deepcopy(trace)
    failsafe['frames'][-1]['flags']=12
    assert causal(failsafe,4)['kind'] == 'failsafe'
    timeout=deepcopy(trace)
    timeout['frames']=[dict(ns=90000000,flags=0),dict(ns=110000000,flags=0)]
    timeout['cycles'][0][1][4]=110000000
    assert causal(timeout,5)['loss_ns'] == 210000000
    bad=[]
    early=deepcopy(trace); early['tx'][-1]=(190000000,0x201,zero); bad.append(early)
    late=deepcopy(trace); late['tx'][-1]=(3050000000,0x201,zero); bad.append(late)
    restart=deepcopy(trace); restart['tx'].append((220000000,0x201,moving)); bad.append(restart)
    x1=deepcopy(trace); x1['rx'].append((180000000,0x181,bytes.fromhex('2794271400003200'),0,0)); bad.append(x1)
    no_motion=deepcopy(trace); no_motion['rx']=[]; bad.append(no_motion)
    missing=deepcopy(trace); missing['frames'][-1]['flags']=0; bad.append(missing)
    armed=deepcopy(trace); armed['cycles'][0][1][14]=1; bad.append(armed)
    cutoff=deepcopy(trace); cutoff['stops'][0]['cause']=2; bad.append(cutoff)
    for invalid in bad:
        try:
            causal(invalid,3)
        except (AssertionError,StopIteration):
            pass
        else:
            raise AssertionError('Invalid causal scenario accepted')
    print(json.dumps(dict(syntax='PASS', ordered_live_prompts='PASS',
                          expired_and_cleanup_prompts_suppressed='PASS',
                          archived_a1_exit_suppressed='PASS',
                          missing_readiness_rejected_before_subprocess='PASS',
                          sbus_causal_positive_cases=3, sbus_causal_rejections=len(bad), physical_started=False), indent=2))


if __name__ == '__main__':
    main()
