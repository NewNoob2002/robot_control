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
    causal = runpy.run_path(str(BASE / 'analyze-x1.py'))['check']
    moving = bytes.fromhex('0f0000000500')
    zero = bytes.fromhex('060000000000')
    speed = bytes.fromhex('2714271400003200')
    x1 = bytes.fromhex('2794271400003200')
    tx = [(100,0x201,moving),(300,0x201,zero)]
    rx = [(150,0x181,speed,0,0),(200,0x181,x1,0,0)]
    assert causal(tx,rx)['x1_ns'] == 200
    for bad_tx,bad_rx in (([(100,0x201,moving),(180,0x201,zero)],rx),
                          (tx+[(400,0x201,moving)],rx),
                          (tx,[(200,0x181,x1,0,0)]),
                          (tx,rx+[(400,0x181,speed,0,0)])):
        try:
            causal(bad_tx,bad_rx)
        except (AssertionError,StopIteration):
            pass
        else:
            raise AssertionError('Invalid causal scenario accepted')
    print(json.dumps(dict(syntax='PASS', ordered_live_prompts='PASS',
                          expired_and_cleanup_prompts_suppressed='PASS',
                          archived_a1_exit_suppressed='PASS',
                          missing_readiness_rejected_before_subprocess='PASS',
                          physical_started=False), indent=2))


if __name__ == '__main__':
    main()
