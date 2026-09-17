"""One prepared zero-target SBUS recovery ControlLoop qualification on the explicitly identified RK3588."""
import hashlib
import json
import signal
import subprocess
import time
from pathlib import Path

BASE = Path(__file__).resolve().parent
SHA = '13ccbd9db41a349fedf2131f6490fc779eed1054d77a4c0ba6fcef490ca6b933'
BINARY = BASE / 'robot-control-hil'
ARGS = ['--interface', 'can0', '--device', '/dev/serial/by-id/usb-1a86_USB_Single_Serial_586D017868-if00', '--duration-ms', '60000', '--zero-uart-recovery', '--zero-feedback-tenths-rpm', '20']


def snapshot():
    """Read interface state and counters without altering the interface."""
    return json.loads(subprocess.check_output(
        ['ip', '-j', '-details', '-statistics', 'link', 'show', 'can0'],
        text=True, timeout=5))[0]


def write_json(path, value):
    """Retain newline-terminated trial evidence."""
    path.write_text(json.dumps(value, indent=2) + chr(10))


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
            capture = subprocess.Popen(['stdbuf', '-oL', 'candump', '-D', '-ta', '-e', '-n', '20000',
                                        'can0,0:0,#FFFFFFFF'], stdout=raw, stderr=err)
            time.sleep(0.25)
            assert capture.poll() is None and not (out / 'candump.stderr').read_text()
            with (out / 'application.log').open('wb') as log:
                started = time.monotonic()
                application = subprocess.Popen([str(BINARY), *ARGS], stdout=log, stderr=subprocess.STDOUT)
                (out / 'application.pid').write_text(str(application.pid) + chr(10))
                control_started = None
                last_remaining = None
                while application.poll() is None:
                    assert capture.poll() is None, 'candump exited during qualification'
                    assert time.monotonic() - started < 90, 'application exceeded 90s guard'
                    assert not (BASE / 'abort').exists(), 'independent observer requested abort'
                    contents = (out / 'application.log').read_text()
                    if application.poll() is not None:
                        break
                    if 'event=stop_cause' in contents or 'phase=control ok=0' in contents:
                        if not result.get('stopping_prompted'):
                            print('APPLICATION_STOPPING: no further operator actions; neutral and wait for cleanup', flush=True)
                            result['stopping_prompted'] = True
                    if not result.get('operator_prompted') and 'event=control_start' in contents:
                        control_started = time.monotonic()
                        result['operator_prompted'] = True
                        print('CONTROL_READY: keep sticks neutral; press CH6 once; ZERO recovery window60s; wait for FAULT_READY', flush=True)
                    if control_started is not None:
                        remaining = max(0, 59 - int(time.monotonic() - control_started))
                        if remaining != last_remaining:
                            print('WINDOW_REMAINING: ' + str(remaining), flush=True)
                            last_remaining = remaining
                    for phase, prompt in (
                        ('fault_ready', 'FAULT_READY: disconnect SBUS SIGNAL ONLY; USB, GROUND AND POWER STAY CONNECTED; transmitter stays ON; keep X1 released; wait for RELEASE_FAULT'),
                        ('release_fault', 'RELEASE_FAULT: neutral sticks and CH6 released, reconnect SBUS SIGNAL to original port; USB, ground and power unchanged; after reconnection hold forward throttle, press/release CH6 once; wait for NEUTRAL_READY'),
                        ('neutral_ready', 'NEUTRAL_READY: return sticks to neutral, keep CH6 released; wait for REARM_READY'),
                        ('rearm_ready', 'REARM_READY: neutral sticks; press/release CH6 once'),
                        ('zero_reenabled', 'ZERO_REENABLED: maintain neutral; wait for cleanup'),
                        ('complete', 'RECOVERY_COMPLETE: zero recovery observed; wait for cleanup')):
                        if not result.get(phase) and ('event=recovery phase='+phase+' ') in contents and last_remaining != 0 and not result.get('stopping_prompted'):
                            result[phase] = True
                            print(prompt, flush=True)
                    time.sleep(0.02)
                print('APPLICATION_EXIT: no further stick stimulus; remain neutral; inspect result', flush=True)
                result['application_exit'] = application.returncode
                result['application_elapsed_ms'] = (time.monotonic() - started) * 1000
            time.sleep(1.0)
            after = snapshot()
            write_json(out / 'can-after.json', after)
            for side in ('rx', 'tx'):
                for key in ('errors', 'dropped', 'over_errors'):
                    assert after['stats64'][side].get(key, 0) == before['stats64'][side].get(key, 0), (side, key)
            assert not any(after['linkinfo']['info_data'].get('berr_counter', {}).values())
            assert application.returncode == 0, 'application failed; inspect log before any retry'
            result['passed'] = True  # Software endpoint only; dual-capture/operator analysis is separate.
    except BaseException as exc:
        result['error'] = str(exc)
        raise
    finally:
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
