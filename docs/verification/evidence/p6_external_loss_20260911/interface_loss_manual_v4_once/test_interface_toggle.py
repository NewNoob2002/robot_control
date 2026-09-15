"""Exercise the actual helper cleanup block without sudo, CAN, or network access."""
import ast
from pathlib import Path
from types import SimpleNamespace

source = (Path(__file__).resolve().parent / 'interface_toggle.py').read_text()
node = next(n for n in ast.parse(source).body if isinstance(n, ast.Try))
code = compile(ast.Module(body=[node], type_ignores=[]), 'interface_toggle_block', 'exec')
for scenario in ('normal', 'interrupted_hold', 'down_failure', 'up_failure'):
    calls, saved = [], []
    def run(argv, **kwargs):
        """Simulate exactly the selected command outcome."""
        calls.append(argv[-1])
        if scenario == 'down_failure' and argv[-1] == 'down':
            raise RuntimeError('down denied')
        return SimpleNamespace(returncode=int(scenario == 'up_failure' and argv[-1] == 'up'))
    def sleep(seconds):
        """Assert the fixed hold and inject interruption when requested."""
        assert seconds == 3
        if scenario == 'interrupted_hold':
            raise KeyboardInterrupt()
    scope = {'subprocess': SimpleNamespace(run=run), 'time': SimpleNamespace(sleep=sleep, time_ns=lambda: 1),
             'save': lambda name, value: saved.append(dict(value)), 'event': {}, 'down': False}
    try:
        exec(code, scope)
    except (KeyboardInterrupt, RuntimeError, AssertionError):
        pass
    assert calls == (['down'] if scenario == 'down_failure' else ['down', 'up'])
    assert saved[-1]['state'] == ('failed' if scenario in ('down_failure', 'up_failure') else 'up')
print('PASS: normal, interrupted hold, down failure, up failure; no hardware access')
