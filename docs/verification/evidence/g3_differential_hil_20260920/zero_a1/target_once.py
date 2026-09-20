"""One prepared zero-only ControlLoop qualification on the explicitly identified RK3588."""
import hashlib
import json
import signal
import select
import os
import sys
import subprocess
import time
from pathlib import Path

BASE = Path(__file__).resolve().parent
SHA = '51f94c1f51945f3d28fa945d58f14cb15ad4ec7a4cab353331a2f39e7ca16451'
BINARY = BASE.parent / 'robot-control-hil'
ARGS = ['--interface', 'can0', '--device', '/dev/serial/by-id/usb-1a86_USB_Single_Serial_586D017868-if00', '--duration-ms', '20000', '--zero-only', '--zero-feedback-tenths-rpm', '20', '--right-sign', '-1']


def snapshot():
    """Read interface state and counters without altering the interface."""
    return json.loads(subprocess.check_output(
        ['ip', '-j', '-details', '-statistics', 'link', 'show', 'can0'],
        text=True, timeout=5))[0]


def write_json(path, value):
    """Retain newline-terminated trial evidence."""
    path.write_text(json.dumps(value, indent=2) + chr(10))


def consume_lease(pending, data):
    """Validate a bounded stream, preserving fragmented leases until complete."""
    assert data, 'host lease closed'
    pending += data
    assert len(pending) <= 4096, 'host lease input too long'
    lines = pending.split(b'\n')
    assert all(line == b'LEASE' for line in lines[:-1]), 'invalid host lease'
    assert b'LEASE'.startswith(lines[-1]), 'invalid partial host lease'
    return lines[-1], bool(lines[:-1])


def enabled_zero_logged(log):
    """Recognize the application enabled-zero status without inferring motion."""
    return any(line.startswith('event=control ') and all(token in line.split() for token in
        ('armed=1', 'word=15', 'left_tenths_rpm=0', 'right_tenths_rpm=0'))
        for line in log.splitlines())


def main():
    """Check fresh readiness, capture before a single bounded invocation and preserve cleanup."""
    assert Path('/etc/machine-id').read_text().strip() == '6923ab3301fb4a8d816759b04ec6bf0a'
    assert hashlib.sha256(BINARY.read_bytes()).hexdigest() == SHA
    auth = json.loads((BASE / 'authorization.json').read_text())
    assert auth['authorized'] and auth['attempts'] == 1 and auth['arguments'] == ARGS
    assert auth['artifact_sha256'] == SHA
    ready = json.loads((BASE / 'operator-ready.json').read_text())
    assert ready['drive_power_on'] and ready['wheels_raised'] and ready['emergency_stop_available']
    assert ready['independent_capture_ready'] and ready['artifact_sha256'] == SHA
    assert 0 <= time.time() - ready['wall_time'] < 120
    assert all(ready[key] for key in ('x1_released', 'sticks_neutral', 'ch6_released',
                                     'transmitter_receiver_online', 'unchanged_wiring',
                                     'operator_display_visible'))
    serial = subprocess.check_output(['udevadm', 'info', '-q', 'property', '-n', ARGS[3]], text=True, timeout=5)
    assert 'ID_SERIAL_SHORT=586D017868' in serial
    for entry in Path('/proc').iterdir():
        if entry.name.isdigit():
            try:
                name = (entry / 'exe').resolve(strict=True).name
                assert name not in ('cansend', 'cangen', 'robot-control-zlac-qualification',
                                    'robot-control-canopen-commission', 'robot-control-hil', 'robot-control-sbus-observer'), name
            except (FileNotFoundError, PermissionError, ProcessLookupError):
                pass
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(RuntimeError('runner terminated')))
    signal.signal(signal.SIGHUP, lambda *_: (_ for _ in ()).throw(RuntimeError('host disconnected')))
    before = snapshot()
    info = before['linkinfo']['info_data']
    assert before['mtu'] == 16 and 'UP' in before['flags'] and info['state'] == 'ERROR-ACTIVE'
    assert info['bittiming']['bitrate'] == 500000 and not any(info.get('berr_counter', {}).values())
    out = BASE / 'physical-once'
    out.mkdir(exist_ok=False)  # Consumed even if a later preflight or capture fails.
    write_json(out / 'can-before.json', before)
    capture = application = None
    result = {'passed': False, 'arguments': ARGS, 'artifact_sha256': SHA}
    try:
        with (out / 'candump.log').open('wb') as raw, (out / 'candump.stderr').open('wb') as err:
            capture = subprocess.Popen(['stdbuf', '-oL', 'candump', '-D', '-ta', '-e', '-n', '10000',
                                        'can0,0:0,#FFFFFFFF'], stdout=raw, stderr=err)
            time.sleep(0.25)
            assert capture.poll() is None and not (out / 'candump.stderr').read_text()
            with (out / 'application.log').open('wb') as log:
                started = time.monotonic()
                application = subprocess.Popen([str(BINARY), *ARGS], stdout=log, stderr=subprocess.STDOUT)
                (out / 'application.pid').write_text(str(application.pid) + chr(10))
                lease_at = time.monotonic()
                lease_pending = b''
                next_tick = lease_at
                while application.poll() is None:
                    if select.select([sys.stdin], [], [], 0)[0]:
                        data = os.read(sys.stdin.fileno(), 4096)
                        lease_pending, complete = consume_lease(lease_pending, data)
                        if complete:
                            lease_at = time.monotonic()
                    assert time.monotonic()-lease_at < 3, 'host lease expired'
                    if time.monotonic() >= next_tick:
                        print('PROGRESS: active', flush=True)
                        next_tick = time.monotonic()+0.5
                    assert capture.poll() is None, 'candump exited during qualification'
                    assert time.monotonic() - started < 45, 'application exceeded 45s guard'
                    assert not (BASE / 'abort').exists(), 'independent observer requested abort'
                    if not result.get('operator_prompted') and 'event=control_start' in (out / 'application.log').read_text():
                        result['operator_prompted'] = True
                        print('CONTROL_READY: 摇杆保持中立；现在按下并释放CH6一次，然后保持中立等待结束。', flush=True)
                    if not result.get('enabled_prompted') and enabled_zero_logged((out / 'application.log').read_text()):
                        result['enabled_prompted'] = True
                        print('ZERO_ENABLED: enabled; targets remain zero; do not press CH6 again', flush=True)
                    time.sleep(0.02)
                result['application_exit'] = application.returncode
                result['application_elapsed_ms'] = (time.monotonic() - started) * 1000
                assert application.returncode == 0, 'application failed; inspect log before any retry'
            time.sleep(1.0)
            after = snapshot()
            write_json(out / 'can-after.json', after)
            for side in ('rx', 'tx'):
                for key in ('errors', 'dropped', 'over_errors'):
                    assert after['stats64'][side].get(key, 0) == before['stats64'][side].get(key, 0), (side, key)
            assert not any(after['linkinfo']['info_data'].get('berr_counter', {}).values())
            result['passed'] = True  # Software endpoint only; dual-capture/operator analysis is separate.
    except BaseException as exc:
        result['error'] = str(exc)
        raise
    finally:
        for sig in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
            signal.signal(sig, signal.SIG_IGN)
        if application is not None and application.poll() is None:
            application.send_signal(signal.SIGTERM)
            try:
                application.wait(timeout=12)
            except subprocess.TimeoutExpired:
                application.kill()
                application.wait(timeout=2)
                result['forced_kill'] = True
                result['operator_power_off_required'] = True
        if capture is not None and capture.poll() is None:
            capture.send_signal(signal.SIGINT)
            capture.wait(timeout=2)
        write_json(out / 'result.json', result)
        print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
