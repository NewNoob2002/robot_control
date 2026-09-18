#!/usr/bin/env python3
"""Supervise one actual-traffic RK3588 soak and independent JCAN capture."""
import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import time

BASE = Path(__file__).resolve().parent
REMOTE = '/home/cat/.cache/robot-control/staging/p6-can-soak-20260918'
CAPTURE = BASE / 'jcan'
STOP = False


def stop(_signum, _frame):
    """Request abort while allowing the target restoration grace period."""
    global STOP
    STOP = True


def main():
    """Lease target execution only while the fresh capture remains healthy."""
    with (BASE / 'CONSUMED').open('x') as marker:
        marker.write(str(time.time()))
    for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, stop)
    capture = target = None
    error = None
    done = None
    started = time.monotonic()
    with (BASE / 'jcan-supervisor.log').open('wb') as cap_log, \
         (BASE / 'target-stream.log').open('wb') as target_log:
        try:
            capture = subprocess.Popen([sys.executable, '-B', str(CAPTURE / 'capture_once.py'), '--run'],
                stdin=subprocess.DEVNULL, stdout=cap_log, stderr=subprocess.STDOUT, start_new_session=True)
            while not (CAPTURE / 'progress.json').exists():
                assert capture.poll() is None and time.monotonic() - started < 20 and not STOP, 'capture startup failed'
                time.sleep(0.1)
            target = subprocess.Popen(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5', 'robot-dev',
                'python3 -B ' + REMOTE + '/target_once.py'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, start_new_session=True)
            buffer = b''
            next_lease = 0
            last_progress = time.monotonic()
            with selectors.DefaultSelector() as selector:
                selector.register(target.stdout, selectors.EVENT_READ)
                while done is None:
                    if STOP or (BASE / 'STOP').exists():
                        raise RuntimeError('operator requested stop')
                    assert capture.poll() is None and not (CAPTURE / 'result.json').exists(), 'JCAN capture exited'
                    assert time.time() - (CAPTURE / 'progress.json').stat().st_mtime < 20, 'JCAN status stale'
                    now = time.monotonic()
                    assert now - last_progress < 15, 'target progress lost'
                    assert now - started < 11040, 'joint run deadline exceeded'
                    if now >= next_lease:
                        target.stdin.write(b'{"op":"lease"}\n')
                        target.stdin.flush()
                        next_lease = now + 2
                    for _key, _mask in selector.select(timeout=0.2):
                        chunk = os.read(target.stdout.fileno(), 65536)
                        if not chunk:
                            raise RuntimeError('target exited without DONE')
                        target_log.write(chunk)
                        target_log.flush()
                        buffer += chunk
                        while b'\n' in buffer:
                            line, buffer = buffer.split(b'\n', 1)
                            if line.startswith(b'PROGRESS '):
                                progress = json.loads(line[9:])
                                last_progress = time.monotonic()
                                assert not progress['lease_error'], progress
                                progress['jcan'] = json.loads((CAPTURE / 'progress.json').read_text())
                                if progress['elapsed_s'] > 30 and progress['jcan']['frames'] == 0:
                                    raise RuntimeError('no actual CAN traffic observed within 30 seconds')
                                temporary = BASE / 'progress.tmp'
                                temporary.write_text(json.dumps(progress, indent=2))
                                temporary.replace(BASE / 'progress.json')
                            elif line.startswith(b'DONE '):
                                done = json.loads(line[5:])
                    if len(buffer) > 1048576:
                        raise RuntimeError('target output line bound exceeded')
            assert done['rc'] == 0 and not done['lease_error'], done
            assert target.wait(timeout=5) == 0, 'SSH/target exit failed'
        except BaseException as exc:
            error = f'{type(exc).__name__}: {exc}'
        finally:
            if target is not None and target.poll() is None:
                try:
                    target.stdin.write(b'{"op":"abort"}\n')
                    target.stdin.flush()
                except (OSError, ValueError):
                    pass
                # Closing stdin also revokes the target lease if abort delivery fails.
                try:
                    tail, _ = target.communicate(timeout=35)
                    target_log.write(tail)
                except subprocess.TimeoutExpired:
                    error = error or 'target cleanup unconfirmed; keep X1 locked'
                    target.terminate()
                    try:
                        target.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        target.kill()
                        target.wait(timeout=2)
            if capture is not None and capture.poll() is None:
                (CAPTURE / 'STOP').touch()
                try:
                    capture.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    error = error or 'JCAN cleanup unconfirmed'
                    capture.terminate()
                    try:
                        capture.wait(timeout=12)
                    except subprocess.TimeoutExpired:
                        capture.kill()
                        capture.wait(timeout=2)
            if (CAPTURE / 'result.json').exists():
                captured = json.loads((CAPTURE / 'result.json').read_text())
                if captured['error'] or not captured['shutdown_ack'] or captured['process_rc'] != 0:
                    error = error or 'JCAN capture/cleanup failed'
            else:
                error = error or 'JCAN result missing'
        result = {'status': 'FAIL' if error else 'AWAITING_DUAL_CAPTURE_AUDIT', 'error': error,
                  'target_done': done, 'elapsed_s': time.monotonic() - started,
                  'scope': 'RK3588 disabled CANopen lifecycle; not integrated SBUS/ControlLoop soak'}
        (BASE / 'result.json').write_text(json.dumps(result, indent=2))
        print(json.dumps(result), flush=True)
        return 1 if error else 0


if __name__ == '__main__':
    sys.exit(main())
