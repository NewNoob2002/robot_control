#!/usr/bin/env python3
"""Bound one authorized bounded JCAN capture for the RK3588 CANopen soak."""
import hashlib
import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone

HERE = Path(__file__).resolve().parent
JCAN = Path('/home/gtc/Desktop/workspace/JCAN/target/release/jcan')
SERIAL = '207F346D5650'
ELF_HASH = '151a84145830dbb79ee8931d7bb10ccf686b7e161d956ca0f4492e5597c19cbc'
DURATION = 11100  # Three-hour target window plus bounded startup/cleanup margin.
STOP_REQUESTED = False


def write_json(name, value):
    """Atomically publish an observation in this one-shot evidence directory."""
    temporary = HERE / (name + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(HERE / name)


def read_only(arguments, name):
    """Retain and validate one bounded read-only command without retry."""
    result = subprocess.run([str(JCAN), '--json', *arguments], capture_output=True, timeout=10)
    (HERE / (name + '.json')).write_bytes(result.stdout)
    (HERE / (name + '.stderr')).write_bytes(result.stderr)
    if result.returncode or result.stderr:
        raise RuntimeError(f'{name}: rc={result.returncode} stderr={result.stderr!r}')
    value = json.loads(result.stdout)
    if value.get('ok') is not True or value.get('warnings'):
        raise RuntimeError(f'{name}: {value}')
    return value['data']


def checked_event(line):
    """Reject malformed data, warnings, disconnections and reconnect attempts."""
    event = json.loads(line)
    if event.get('ok') is False or event.get('error') or event.get('warnings'):
        raise RuntimeError(str(event))
    if event.get('event') not in ('session_started', 'connected', 'frame'):
        if event.get('ok') is not True or event.get('op') not in ('status', 'shutdown'):
            raise RuntimeError(str(event))
    return event


def request_stop(_signum, _frame):
    """Request graceful cleanup without accepting an interrupted run."""
    global STOP_REQUESTED
    STOP_REQUESTED = True


def main():
    """Monitor one session, stop on first failure, and preserve configuration."""
    with (HERE / 'CONSUMED').open('x') as marker:
        marker.write(datetime.now(timezone.utc).isoformat() + '\n')
    process = baseline = connected_at = pending_status = None
    frames = 0
    error = None
    complete = False
    shutdown_ack = False
    started = time.monotonic()
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, request_stop)
    with (HERE / 'session.jsonl').open('wb') as raw, (HERE / 'session.stderr').open('wb') as stderr, \
         (HERE / 'requests.jsonl').open('w') as requests:
        try:
            if hashlib.sha256(JCAN.read_bytes()).hexdigest() != ELF_HASH:
                raise RuntimeError('JCAN binary identity changed')
            if not read_only(['self-test'], 'self_test')['passed']:
                raise RuntimeError('self-test failed')
            devices = read_only(['scan'], 'scan')
            if sum(item['serial'] == SERIAL for item in devices) != 1:
                raise RuntimeError('adapter identity mismatch')
            baseline = read_only(['--serial', SERIAL, 'config-get'], 'config_pre')
            if any(baseline[key] != value for key, value in
                   {'can_speed': '0C', 'standard': '00', 'term_res': '00'}.items()):
                raise RuntimeError('baseline differs from reviewed setup')
            command = [str(JCAN), '--json', '--serial', SERIAL, 'session', '--mode', 'normal',
                       '--receive', '--reconnect-ms', '60000']
            write_json('preflight.json', {
                'started_utc': datetime.now(timezone.utc).isoformat(), 'serial': SERIAL,
                'binary_sha256': ELF_HASH, 'command': command, 'duration_s': DURATION,
                'authorization': 'User requested JCAN stability test and supplied current readiness',
                'operator_state': {'drive_power': 'ON', 'x1_locked': True, 'continuous_can_traffic': 'produced by RK3588 during target run'},
                'scope': 'Normal receive with hardware ACK; no data-frame sends or drive configuration',
                'recovery': 'First error aborts; shutdown/CANStop; no intentional reconnect or retry',
                'limitation': 'Companion capture only; target result and dual-capture audit determine acceptance'})
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                       stderr=stderr, start_new_session=True)
            buffer = b''
            next_status = time.monotonic()
            sequence = 0
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ)
                while not STOP_REQUESTED and not (HERE / 'STOP').exists():
                    now = time.monotonic()
                    if connected_at is None and now - started > 15:
                        raise RuntimeError('connection deadline exceeded')
                    if pending_status is not None and now - pending_status[1] > 10:
                        raise RuntimeError('status response deadline exceeded')
                    if stderr.tell():
                        raise RuntimeError('JCAN stderr is not empty')
                    if connected_at is not None and now - connected_at >= DURATION:
                        complete = True
                        break
                    if connected_at is not None and pending_status is None and now >= next_status:
                        sequence += 1
                        request = json.dumps({'id': sequence, 'op': 'status'}) + '\n'
                        requests.write(request)
                        requests.flush()
                        process.stdin.write(request.encode())
                        process.stdin.flush()
                        pending_status = (sequence, now)
                        next_status = now + 10
                    for _key, _mask in selector.select(timeout=0.2):
                        chunk = os.read(process.stdout.fileno(), 65536)
                        if not chunk:
                            raise RuntimeError('unexpected session EOF')
                        raw.write(chunk)
                        raw.flush()
                        buffer += chunk
                        while b'\n' in buffer:
                            line, buffer = buffer.split(b'\n', 1)
                            event = checked_event(line)
                            if event.get('event') == 'connected':
                                if connected_at is not None or event['serial'] != SERIAL or event['mode'] != 'normal':
                                    raise RuntimeError('unexpected connection identity/generation')
                                connected_at = time.monotonic()
                                print('RUNNING: normal receive; RK3588 companion capture; no data sends', flush=True)
                            elif event.get('event') == 'frame':
                                frames += 1
                            elif event.get('op') == 'status':
                                state = event['data']
                                if pending_status is None or event['id'] != pending_status[0] or not all(
                                        state.get(key) is True for key in ('connected', 'running', 'receive_enabled')):
                                    raise RuntimeError('invalid session status response')
                                if state.get('mode') != 'normal':
                                    raise RuntimeError('unexpected mode')
                                pending_status = None
                                write_json('progress.json', {'status': 'RUNNING', 'elapsed_s': time.monotonic() - connected_at,
                                                           'frames': frames, 'observed_utc': datetime.now(timezone.utc).isoformat()})
                    if len(buffer) > 1048576 or raw.tell() > 268435456:
                        raise RuntimeError('capture size bound exceeded')
        except BaseException as exc:
            error = f'{type(exc).__name__}: {exc}'
        finally:
            if process is not None:
                try:
                    request = json.dumps({'id': 'finish', 'op': 'shutdown'}) + '\n'
                    requests.write(request)
                    requests.flush()
                    tail, _ = process.communicate(input=request.encode(), timeout=5)
                    raw.write(tail)
                    for line in tail.splitlines():
                        event = checked_event(line)
                        shutdown_ack |= event.get('id') == 'finish' and event.get('op') == 'shutdown'
                    if process.returncode != 0 or not shutdown_ack:
                        raise RuntimeError(f'cleanup unverified: rc={process.returncode}, ack={shutdown_ack}')
                except BaseException as exc:
                    error = error or f'cleanup: {exc}'
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=2)
            if stderr.tell():
                error = error or 'JCAN stderr is not empty'
            if baseline is not None:
                try:
                    if read_only(['--serial', SERIAL, 'config-get'], 'config_post') != baseline:
                        raise RuntimeError('configuration changed')
                except BaseException as exc:
                    error = error or f'postflight: {exc}'
    result = {'status': 'FAIL' if error else 'CAPTURE_TIME_LIMIT' if complete else 'INTERRUPTED',
              'error': error, 'requested_s': DURATION, 'elapsed_s': time.monotonic() - (connected_at or started),
              'frames': frames, 'data_frame_commands': 0, 'shutdown_ack': shutdown_ack,
              'process_rc': None if process is None else process.returncode,
              'finished_utc': datetime.now(timezone.utc).isoformat(),
              'limitation': 'No proof of permanent repair; USB raw transfers not captured; use joint target/capture result'}
    write_json('result.json', result)
    print(json.dumps(result), flush=True)
    return 1 if error or not complete else 0


def self_check():
    """Exercise the event gate offline without opening USB."""
    for line in ('{"event":"session_started"}', '{"event":"connected"}',
                 '{"ok":true,"op":"status"}', '{"ok":true,"op":"shutdown"}'):
        checked_event(line)
    for line in ('invalid', '{"event":"disconnected"}', '{"event":"reconnecting"}',
                 '{"ok":false,"error":"bad length"}', '{"event":"frame","warnings":["bad"]}'):
        try:
            checked_event(line)
        except (RuntimeError, ValueError):
            continue
        raise AssertionError('invalid event accepted')
    print('PASS: session event gate; no USB operation')


if __name__ == '__main__':
    if sys.argv[1:] == ['--self-check']:
        self_check()
    elif sys.argv[1:] == ['--run']:
        sys.exit(main())
    else:
        sys.exit('Use --self-check or --run (one-shot hardware operation)')
