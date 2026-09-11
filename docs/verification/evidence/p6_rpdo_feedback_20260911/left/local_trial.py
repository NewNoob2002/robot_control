#!/usr/bin/env python3
"""Coordinate one approved synchronous packed-target run and two bounded passive captures."""
import json, queue, subprocess, sys, threading, time
from pathlib import Path
OUT = Path(__file__).resolve().parent
JCAN = '/home/gtc/Desktop/workspace/JCAN/target/release/jcan'
SERIAL = '207F346D5650'
BASE = '/tmp/robot-control-qualifications/rpdo-feedback-0e835fd8a9bc'
SSH = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5', 'robot-dev']
authorization = json.loads((OUT / 'authorization.json').read_text())
assert authorization['authorized'] is True and authorization['stimulus'] == 'left'
assert authorization['elf_sha256'] == '0e835fd8a9bcf1907a59e78a7fb753af791be5dfec1a8839fc64b66bb51ad659'
assert authorization['operator_ready'] is True and authorization['user_confirmation']
(OUT / 'run_once.marker').touch(exist_ok=False)
events = queue.Queue()
threads = []
remote = observer = None
error = None
go = False
started = False
frames = downloads = uploads = nmts = nonzero = terminal = rpdo_frames = 0
connected = receive_enabled = False
baseline = None

def read_lines(stream, label, path):
    """Archive a process stream and publish its lines to the coordinator."""
    with path.open('wb') as log:
        for line in iter(stream.readline, b''):
            log.write(line)
            log.flush()
            events.put((label, line))
    events.put((label + '_eof', b''))

def attach(process, prefix):
    """Start bounded-lifetime readers for one owned child process."""
    for stream, suffix in ((process.stdout, 'out'), (process.stderr, 'err')):
        label = prefix + '_' + suffix
        path = OUT / ('jcan_session.jsonl' if label == 'jcan_out' else label + '.log')
        thread = threading.Thread(target=read_lines, args=(stream, label, path), daemon=True)
        threads.append(thread)
        thread.start()

def jcan_read(arguments, filename):
    """Run and archive a read-only JCAN command without retry."""
    command = [JCAN, '--json', *arguments]
    result = subprocess.run(command, capture_output=True, timeout=10)
    (OUT / filename).write_bytes(result.stdout)
    (OUT / (filename + '.stderr')).write_bytes(result.stderr)
    assert result.returncode == 0 and not result.stderr, result.stderr.decode()
    data = json.loads(result.stdout)
    assert data.get('ok') is True and not data.get('warnings'), data
    return data['data']

def validate_frame(frame):
    """Reject traffic outside the exact approved frame set and count bounds."""
    global frames, downloads, uploads, nmts, nonzero, terminal, rpdo_frames
    frames += 1
    assert frames <= 10000, 'capture frame bound'
    assert not any(frame.get(k, False) for k in ('extended', 'fd', 'brs', 'remote')), frame
    ident = frame['can_id']
    payload = bytes.fromhex(frame['data_hex'])
    assert ident in (0, 0x201, 0x181, 0x281, 0x381, 0x481, 0x581, 0x601, 0x701), frame
    if ident in (0x281, 0x381, 0x481):
        assert len(payload) == 0, frame
    if ident == 0:
        assert go and payload in (bytes([1, 1]), bytes([0x80, 1])), frame
        nmts += 1
        assert nmts <= 3
    if ident == 0x201:
        rpdo_frames += 1
        assert rpdo_frames <= 2
        assert go and payload in (bytes.fromhex('0F0005000000'), bytes.fromhex('060000000000')), frame
        if payload[0] == 15:
            nonzero += 1
            assert nonzero == 1
        return
    if ident == 0x581:
        assert len(payload) == 8 and payload[0] != 0x80, frame
    if ident != 0x601:
        return
    assert go and len(payload) == 8, 'request before GO or malformed request'
    command, index, sub = payload[0], int.from_bytes(payload[1:3], 'little'), payload[3]
    if command == 0x40:
        allowed = {(0x6060, 0), (0x6061, 0), (0x1017, 0), (0x603F, 0), (0x6041, 0), (0x200F, 0), (0x60FF, 1), (0x60FF, 2), (0x60FF, 3)}
        allowed |= {(0x1400, i) for i in (1, 2, 5)} | {(0x1600, i) for i in (0, 1, 2)}
        allowed |= {(0x606C, i) for i in (1, 2, 3)}
        allowed |= {(0x1800, i) for i in (1, 2, 5)} | {(0x1A00, i) for i in (0, 1, 2)}
        assert (index, sub) in allowed and payload[4:] == bytes(4), frame
        uploads += 1
        assert uploads <= 512
        return
    downloads += 1
    assert downloads <= 32
    value = int.from_bytes(payload[4:], 'little')
    allowed = {(0x2B, 0x1017, 0, 0), (0x2B, 0x1017, 0, 500),
               (0x2F, 0x6060, 0, 3), (0x2B, 0x6040, 0, 6),
               (0x2B, 0x6040, 0, 7), (0x2B, 0x6040, 0, 15),
               (0x23, 0x60FF, 3, 0)}
    if authorization['stimulus'] != 'zero':
        channel = 1 if authorization['stimulus'] == 'left' else 2
        allowed |= {(0x23, 0x1400, 1, 0x80000201), (0x23, 0x1400, 1, 0x201),
                    (0x2F, 0x1600, 0, 0), (0x2F, 0x1600, 0, 2),
                    (0x23, 0x1600, 1, 0x60400010), (0x23, 0x1600, 2, 0x60FF0320),
                    (0x23, 0x1600, 2, 0x60600008)}
    assert (command, index, sub, value) in allowed, frame
    if index == 0x60FF and value != 0:
        nonzero += 1
        assert nonzero <= 1
    if index == 0x6040 and value == 6:
        terminal += 1



try:
    assert jcan_read(['self-test'], 'jcan_self_test.json')['passed']
    scan = jcan_read(['scan'], 'jcan_scan.json')
    assert any(device['serial'] == SERIAL for device in scan)
    baseline = jcan_read(['--serial', SERIAL, 'config-get'], 'jcan_config_pre.json')
    assert baseline['can_speed'] == '0C' and baseline['standard'] == '00' and baseline['term_res'] == '00', baseline
    observer = subprocess.Popen([JCAN, '--json', '--serial', SERIAL, 'session', '--mode', 'silent', '--receive'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    attach(observer, 'jcan')
    deadline = time.monotonic() + 40
    startup_deadline = time.monotonic() + 15
    while True:
        assert time.monotonic() < deadline, 'bounded capture deadline'
        assert observer.poll() is None, 'JCAN session exited unexpectedly'
        if not go:
            assert time.monotonic() < startup_deadline, 'capture readiness deadline'
        try:
            label, line = events.get(timeout=0.02)
        except queue.Empty:
            if remote is not None and remote.poll() is not None:
                assert remote.returncode == 0, 'remote trial failed'
                break
            continue
        if label in ('jcan_err', 'remote_err'):
            raise RuntimeError(label + ': ' + line.decode(errors='replace'))
        if label == 'jcan_out':
            data = json.loads(line)
            assert data.get('ok') is not False and not data.get('warnings') and not data.get('error'), data
            event = data.get('event')
            if event == 'session_started':
                assert data['serial'] == SERIAL and data['receive_enabled'] is True
                receive_enabled = True
            elif event == 'connected':
                assert not connected and data['serial'] == SERIAL and data['mode'] == 'silent', data
                connected = True
            elif event == 'frame':
                validate_frame(data)
            else:
                raise RuntimeError('unexpected session event: ' + str(data))
            if connected and receive_enabled and remote is None:
                remote = subprocess.Popen([*SSH, 'python3', '-u', BASE + '/remote_motion_left.py'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                attach(remote, 'remote')
                print('JCAN_READY: silent observer connected', flush=True)
        elif label == 'remote_out':
            message = line.decode().strip()
            print(message, flush=True)
            if message == 'CAPTURE_READY':
                assert connected and receive_enabled and not go
                go = True
                remote.stdin.write(('GO' + chr(10)).encode())
                remote.stdin.flush()
            elif message == 'EXECUTOR_STARTED':
                started = True
            elif message.startswith('APP '):
                pass
            elif not message.startswith('TRIAL_FINISHED rc=0'):
                raise RuntimeError(message)
        elif label == 'jcan_out_eof':
            raise RuntimeError('JCAN disconnected')
    assert started and nonzero == (0 if authorization['stimulus'] == 'zero' else 1) and terminal == 2 and rpdo_frames == 2
except Exception as exc:
    error = str(exc)
    print('STOP: ' + error, flush=True)
    if go:
        print('Operator: use the independent power cut if stopping or cleanup is uncertain.', flush=True)
finally:
    if remote is not None and remote.poll() is None:
        remote.stdin.close()
        try:
            remote.wait(timeout=9)
        except subprocess.TimeoutExpired:
            remote.terminate()
            error = (error or '') + ' remote cleanup unavailable: operator power cut required'
    if observer is not None and observer.poll() is None:
        try:
            request = json.dumps({'id': 99, 'op': 'shutdown'}) + chr(10)
            (OUT / 'jcan_requests.jsonl').write_text(request)
            observer.stdin.write(request.encode())
            observer.stdin.flush()
            observer.wait(timeout=5)
        except (BrokenPipeError, subprocess.TimeoutExpired) as exc:
            observer.stdin.close()
            error = (error or '') + ' JCAN cleanup error: ' + str(exc)
        if observer.poll() is None:
            observer.terminate()
            observer.wait(timeout=3)
        if observer.returncode != 0:
            error = (error or '') + ' JCAN nonzero exit'
    for thread in threads:
        thread.join(timeout=2)
    if remote is not None:
        transfer = subprocess.run(['scp', '-q', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5', '-r', 'robot-dev:' + BASE + '/motion_left', str(OUT / 'target')], capture_output=True, timeout=20)
        (OUT / 'transfer.stderr').write_bytes(transfer.stderr)
        if transfer.returncode != 0:
            error = (error or '') + ' target evidence transfer failed'
    if baseline is not None:
        try:
            assert jcan_read(['--serial', SERIAL, 'config-get'], 'jcan_config_post.json') == baseline
        except Exception as exc:
            error = (error or '') + ' JCAN postflight failed: ' + str(exc)
    (OUT / 'coordinator_result.json').write_text(json.dumps({'go_sent': go, 'executor_started': started, 'error': error, 'frames': frames, 'downloads': downloads, 'uploads': uploads, 'nmt_frames': nmts, 'nonzero_requests': nonzero, 'cleanup_shutdown_requests': terminal}, indent=2))
print('COORDINATOR_FINISHED error=' + str(error), flush=True)
sys.exit(1 if error else 0)
