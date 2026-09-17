"""One prepared right-wheel moving SIGTERM ControlLoop qualification on the explicitly identified RK3588."""
import hashlib
import os
import json
import signal
import re
import subprocess
import time
from pathlib import Path

BASE = Path(__file__).resolve().parent
SHA = '91d3ce99c06b4f96148a1e8cc86412459d53fe1bce88a91ec41f1d64dc33430c'
BINARY = BASE / 'robot-control-hil'
ARGS = ['--interface', 'can0', '--device', '/dev/serial/by-id/usb-1a86_USB_Single_Serial_586D017868-if00', '--duration-ms', '60000', '--right-throttle', '--zero-feedback-tenths-rpm', '20']


def snapshot():
    """Read interface state and counters without altering the interface."""
    return json.loads(subprocess.check_output(
        ['ip', '-j', '-details', '-statistics', 'link', 'show', 'can0'],
        text=True, timeout=5))[0]


def write_json(path, value):
    """Retain newline-terminated trial evidence."""
    path.write_text(json.dumps(value, indent=2) + chr(10))


def signal_due(contents, now, moving_since):
    """Wait300ms after observed positive feedback; stopped/nonpositive samples cancel."""
    samples = re.findall(r'^event=control .* left_tenths_rpm=(-?\d+) right_tenths_rpm=(-?\d+) ', contents, re.MULTILINE)
    stopped = any(marker in contents for marker in ('event=stop_cause', 'phase=control ok=0', 'event=motion_stop'))
    positive = bool(samples) and abs(int(samples[-1][0])) <= 20 and 20 < int(samples[-1][1]) <= 75
    if stopped or not positive:
        return None, False
    moving_since = now if moving_since is None else moving_since
    return moving_since, now-moving_since >= 0.3


def signal_child(application, out, moving_since):
    """Signal only the unreaped, verified child; retain a monotonic syscall bracket."""
    assert application.poll() is None
    proc = Path('/proc') / str(application.pid)
    assert (proc/'exe').resolve(strict=True) == BINARY.resolve(strict=True)
    assert (proc/'cmdline').read_bytes().split(bytes(1))[:-1] == [os.fsencode(BINARY), *map(os.fsencode, ARGS)]
    stat = (proc/'stat').read_text().rsplit(')', 1)[1].split()
    assert int(stat[1]) == os.getpid(), 'not a direct child'
    event = dict(pid=application.pid, process_start_ticks=int(stat[19]),
                 artifact_sha256=SHA, executable=str(BINARY), arguments=ARGS,
                 positive_observed_ns=int(moving_since*1e9), signal=signal.SIGTERM,
                 monotonic_before_ns=time.monotonic_ns(), wall_before_ns=time.time_ns())
    with (out/'sigterm-event.json').open('x') as log:
        json.dump(event, log, indent=2)
    application.send_signal(signal.SIGTERM)
    event.update(monotonic_after_ns=time.monotonic_ns(), wall_after_ns=time.time_ns(), sent=True)
    write_json(out/'sigterm-event.json', event)
    return event


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
                moving_since = None
                displayed_input_state = None
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
                    if not result.get('operator_prompted') and 'event=control_start' in contents and not result.get('stopping_prompted'):
                        control_started = time.monotonic()
                        result['operator_prompted'] = True
                        print('INPUT_WAIT: waiting healthy neutral SBUS; turn transmitter ON if needed; release CH6; total window60s', flush=True)
                    if control_started is not None:
                        remaining = max(0, 59 - int(time.monotonic() - control_started))
                        if remaining != last_remaining:
                            print('WINDOW_REMAINING: ' + str(remaining), flush=True)
                            last_remaining = remaining
                    input_states = re.findall(r'^event=input_status state=(\w+) ', contents, re.MULTILINE)
                    if input_states and input_states[-1] != displayed_input_state and not result.get('stopping_prompted'):
                        displayed_input_state = input_states[-1]
                        prompts = {'waiting_link': 'INPUT_WAIT: turn transmitter ON; neutral and release CH6; waiting healthy input',
                                   'neutral_required': 'INPUT_NEUTRAL: return both sticks to neutral; do not press CH6',
                                   'release_ch6': 'INPUT_RELEASE: release CH6; wait for neutral authorization cue',
                                   'ready': 'CONTROL_READY: healthy neutral input; press CH6 once then release',
                                   'arming': 'ARMING: new neutral authorization received; keep neutral until MOTION_READY'}
                        assert displayed_input_state in prompts
                        print(prompts[displayed_input_state], flush=True)
                    for phase, marker, prompt in (
                        ('motion_ready', 'event=motion_ready', 'MOTION_READY: forward throttle ONLY; steering neutral; hold until SIGNAL_SENT then neutral; automatic SIGTERM after300ms positive feedback; maximum3s; transmitter and receiver ON'),
                        ('stop_verified', 'event=motion_stop verified=1', 'STOP_VERIFIED: neutral; keep transmitter and receiver ON; wait for cleanup then power OFF'),
                        ('stop_not_verified', 'event=motion_stop verified=0', 'STOP_NOT_VERIFIED: emergency stop and power OFF immediately')):
                        if not result.get(phase) and marker in contents and (phase == 'stop_not_verified' or (last_remaining != 0 and not result.get('stopping_prompted'))):
                            result[phase] = True
                            print(prompt, flush=True)
                    moving_since, due = signal_due(contents, time.monotonic(), moving_since)
                    if due and result.get('motion_ready') and not result.get('signal_sent') and not result.get('stopping_prompted') and last_remaining != 0:
                        result['signal_event'] = signal_child(application, out, moving_since)
                        result['signal_sent'] = True
                        print('SIGNAL_SENT: SIGTERM delivered to verified child; neutral now; no CH6; wait for cleanup then drive OFF', flush=True)
                    if result.get('signal_sent'):
                        assert time.monotonic_ns()-result['signal_event']['monotonic_after_ns'] < 12000000000, 'SIGTERM cleanup exceeded12s'
                    time.sleep(0.02)
                print('APPLICATION_EXIT: no further stick stimulus; remain neutral; inspect result', flush=True)
                result['application_exit'] = application.returncode
                result['application_reaped_ns'] = time.monotonic_ns()
                result['application_elapsed_ms'] = (time.monotonic() - started) * 1000
            time.sleep(1.0)
            after = snapshot()
            write_json(out / 'can-after.json', after)
            for side in ('rx', 'tx'):
                for key in ('errors', 'dropped', 'over_errors'):
                    assert after['stats64'][side].get(key, 0) == before['stats64'][side].get(key, 0), (side, key)
            assert not any(after['linkinfo']['info_data'].get('berr_counter', {}).values())
            assert application.returncode in (0, 1), 'unexpected application termination'
            for line in (out / 'application.log').read_text().splitlines():
                if line.startswith('event=feedback_review'):
                    print(line, flush=True)
            result['acceptance'] = 'PENDING independent motion/SIGTERM/feedback/restoration audit'
            result['passed'] = application.returncode == 0  # Preserve application result; wrapper completion is not acceptance.
    except BaseException as exc:
        result['error'] = str(exc)
        raise
    finally:
        if application is not None and application.poll() is None:
            if not result.get('signal_sent'):
                application.send_signal(signal.SIGTERM)
                result['abort_signal_sent'] = True
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
