#!/usr/bin/env python3
"""Display local manual timing; never open CAN or launch the qualification program."""
import json
import os
import time
from pathlib import Path

BASE = Path(__file__).resolve().parent
OUT = BASE / 'cable_loss_once'
ELF = 'acc9f8828f657d968cb2c56a586b1f9e1f2c50250f6c1385d3e6b7439abed46f'

def main():
    """Publish fresh operator readiness and show bounded application timing."""
    assert Path('/etc/machine-id').read_text().strip() == '6923ab3301fb4a8d816759b04ec6bf0a'
    assert not OUT.exists(), 'One-shot trial already consumed'
    print('Raised unloaded wheels, emergency stop ready, drive powered, CAN branch connected.')
    print('Right +5rpm only, left zero. On DISCONNECT NOW and observed right motion, unplug ONLY RK3588 CAN branch.')
    print('After normal stop, reconnect about 3s after unplug; observe no restart for 3s, then POWER OFF.')
    print('Abnormal motion/stop: emergency stop and power off immediately; do not reconnect.')
    assert input('Confirm these conditions and sequence by typing READY: ').strip() == 'READY'
    ready = BASE / 'operator_ready.json'
    ready.write_text(json.dumps({'pid': os.getpid(), 'wall_time': time.time(), 'elf_sha256': ELF}))
    print('WATCH_READY: reply READY in chat; waiting for single trial.', flush=True)
    deadline = time.monotonic() + 180
    armed = False
    outcome_reported = False
    try:
        while time.monotonic() < deadline:
            if (OUT / 'armed.json').exists() and not armed:
                event = json.loads((OUT / 'armed.json').read_text())
                assert event['elf_sha256'] == ELF and time.time() - event['wall_time'] < 2
                print('\aDISCONNECT NOW: only if RIGHT wheel is visibly moving. Follow stop/reconnect/power-off sequence.', flush=True)
                armed = True
            if (OUT / 'outcome.json').exists() and not outcome_reported:
                outcome = json.loads((OUT / 'outcome.json').read_text())
                print('EXECUTOR: ' + outcome['outcome'] + '; finish agreed stationary observation then POWER OFF.', flush=True)
                outcome_reported = True
            if (OUT / 'wrapper_result.json').exists():
                result = json.loads((OUT / 'wrapper_result.json').read_text())
                print('DONE: capture finished. Confirm drive POWER OFF, both wheels stopped and no restart. ' + json.dumps(result), flush=True)
                return
            time.sleep(0.05)
        raise TimeoutError('Readiness expired; no repeat is authorized')
    finally:
        ready.unlink(missing_ok=True)

if __name__ == '__main__':
    main()
