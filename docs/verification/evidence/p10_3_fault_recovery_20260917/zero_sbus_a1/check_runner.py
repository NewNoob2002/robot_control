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
    print(json.dumps(dict(syntax='PASS', ordered_live_prompts='PASS',
                          expired_and_cleanup_prompts_suppressed='PASS',
                          archived_a1_exit_suppressed='PASS',
                          missing_readiness_rejected_before_subprocess='PASS',
                          physical_started=False), indent=2))


if __name__ == '__main__':
    main()
