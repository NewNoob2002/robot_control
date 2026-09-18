#!/usr/bin/env python3
"""Run one RK3588 zero-motion soak with a host capture lease."""
import hashlib
import json
from pathlib import Path
import sys
import threading
import time
from types import SimpleNamespace
import phase6_zero_motion_soak as soak

BASE = Path(__file__).resolve().parent
SCRIPT_SHA = '8991c0d4147c188485a13b110c00a461f180a626631593e38716e1b2e8445604'
last_lease = time.monotonic()
lease_ready = threading.Event()
finished = threading.Event()
lease_failure = None


def receive_lease():
    """Accept only lease/abort messages; EOF revokes continued execution."""
    global last_lease, lease_failure
    try:
        for line in sys.stdin:
            request = json.loads(line)
            if request != {'op': 'lease'}:
                raise RuntimeError('host requested abort or invalid lease')
            last_lease = time.monotonic()
            lease_ready.set()
        if not finished.is_set():
            raise RuntimeError('host monitor disconnected')
    except BaseException as exc:
        lease_failure = str(exc)
        soak.STOP_REQUESTED = True


def monitor():
    """Emit progress and stop if the independent capture lease expires."""
    global lease_failure
    start = time.monotonic()
    while not finished.wait(1):
        if time.monotonic() - last_lease > 10:
            lease_failure = 'host capture lease expired'
            soak.STOP_REQUESTED = True
        output = BASE / 'target-result'
        cycles = output / 'cycle_results.jsonl'
        capture = output / 'rk3588_can.log'
        progress = {'elapsed_s': time.monotonic() - start, 'lease_error': lease_failure,
                    'cycles': len(cycles.read_text().splitlines()) if cycles.exists() else 0,
                    'capture_bytes': capture.stat().st_size if capture.exists() else 0}
        print('PROGRESS ' + json.dumps(progress), flush=True)


def main():
    """Verify exact artifacts before consuming this target trial once."""
    assert Path('/etc/machine-id').read_text().strip() == soak.MACHINE_ID
    assert hashlib.sha256((BASE / 'phase6_zero_motion_soak.py').read_bytes()).hexdigest() == SCRIPT_SHA
    with (BASE / 'CONSUMED').open('x') as marker:
        marker.write(str(time.time()))
    threading.Thread(target=receive_lease, daemon=True).start()
    assert lease_ready.wait(5) and not soak.STOP_REQUESTED, 'initial capture lease missing'
    threading.Thread(target=monitor, daemon=True).start()
    try:
        result = soak.run_soak(SimpleNamespace(confirm='X1_LOCKED_WHEELS_RAISED', hours=3,
            interface='can0', elf=soak.DEFAULT_ELF, output=BASE / 'target-result'))
    except BaseException as exc:
        result = 1
        soak.write_json(BASE / 'wrapper-failure.json', {'error': str(exc)})
    finally:
        finished.set()
    print('DONE ' + json.dumps({'rc': result, 'lease_error': lease_failure}), flush=True)
    return result or (1 if lease_failure else 0)


if __name__ == '__main__':
    sys.exit(main())
