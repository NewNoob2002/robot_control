#!/usr/bin/env python3
"""Run the single authorized synchronous packed-target trial with bounded passive capture."""
import hashlib, json, os, select, signal, subprocess, sys, time
from pathlib import Path
BASE = Path('/tmp/robot-control-qualifications/interface-v5-2304c1c9892d')
ELF = BASE / 'robot-control-zlac-qualification'
EXPECTED = '2304c1c9892d327f73c65304601adfd4ced30e81fb2d4f3bfd1e2c5966ae857c'
authorization = json.loads((BASE / 'authorization_interface_loss_manual_v5_once.json').read_text())
assert authorization['args'] == ['--interface', 'can0', '--external-loss-once']
assert authorization['authorized'] is True and authorization['elf_sha256'] == EXPECTED
assert hashlib.sha256(ELF.read_bytes()).hexdigest() == EXPECTED
assert Path('/etc/machine-id').read_text().strip() == '6923ab3301fb4a8d816759b04ec6bf0a'
for p in Path('/proc').iterdir():
    if not p.name.isdigit():
        continue
    try:
        name = (p / 'exe').resolve(strict=True).name
        assert name not in ('cansend', 'cangen', 'robot-control-zlac-qualification', 'robot-control-canopen-commission'), (p.name, name)
    except (FileNotFoundError, PermissionError, ProcessLookupError):
        pass
OUT = BASE / 'interface_loss_manual_v5_once'
OUT.mkdir(exist_ok=False)
gate = BASE / 'interface_manual_v5_gate'
ready_path = gate / 'ready.json'
assert ready_path.stat().st_uid == 0, 'privileged terminal not ready'
ready = json.loads(ready_path.read_text())
assert ready['uid'] == 0 and time.time() - ready['wall_time'] < 840
assert Path('/proc', str(ready['pid'])).exists(), 'privileged terminal exited'
pre = subprocess.check_output(['ip', '-j', '-details', '-statistics', 'link', 'show', 'can0'], text=True)
(OUT / 'can_preflight.json').write_text(pre)
can = json.loads(pre)[0]
assert 'UP' in can['flags'] and can['mtu'] == 16
info = can['linkinfo']['info_data']
assert info['state'] == 'ERROR-ACTIVE' and info['bittiming']['bitrate'] == 500000
stats = can.get('stats64', can.get('stats', {}))
assert all(stats[d][k] == 0 for d in ('rx', 'tx') for k in ('errors', 'dropped'))
assert all(v == 0 for v in info.get('berr_counter', {}).values())
(OUT / 'elf_sha256.txt').write_text(EXPECTED)
def counter_snapshot(name):
    """Record kernel counters and local capture progress without sending CAN traffic."""
    before = time.monotonic_ns()
    data = json.loads(subprocess.check_output(['ip', '-j', '-details', '-statistics', 'link', 'show', 'can0'], text=True))
    snapshot = {'before_monotonic_ns': before, 'after_monotonic_ns': time.monotonic_ns(), 'can': data,
                'capture_file_bytes': (OUT / 'rk3588_can.log').stat().st_size if (OUT / 'rk3588_can.log').exists() else 0}
    (OUT / (name + '.json')).write_text(json.dumps(snapshot, indent=2))

cap = child = None
rc = 1
started = False
signal_sent = False
link_restored = False
link_down_at = None
target_seen = None
moving_seen = False
try:
    with (OUT / 'rk3588_can.log').open('wb') as raw, (OUT / 'capture.stderr').open('wb') as err:
        cap = subprocess.Popen(['candump', '-D', '-ta', '-e', '-n', '100000', 'can0,0:0,#FFFFFFFF'], stdout=raw, stderr=err)
        time.sleep(0.25)
        assert cap.poll() is None, 'candump failed before readiness'
        assert any(os.readlink(p).startswith('socket:') for p in Path('/proc', str(cap.pid), 'fd').iterdir()), 'capture socket not open'
        counter_snapshot('counters_capture_ready')
        print('CAPTURE_READY', flush=True)
        assert select.select([sys.stdin], [], [], 10)[0], 'GO deadline'
        assert sys.stdin.readline().strip() == 'GO', 'GO missing'
        assert cap.poll() is None and (OUT / 'capture.stderr').stat().st_size == 0
        counter_snapshot('counters_before_executor')
        (OUT / 'attempt_started.txt').write_text(str(time.time()))
        with (OUT / 'executor.log').open('wb') as log:
            child = subprocess.Popen([str(ELF), *authorization['args']], stdout=log, stderr=subprocess.STDOUT)
            started = True
            print('EXECUTOR_STARTED', flush=True)
            end = time.monotonic() + 30
            reported_lines = 0
            while child.poll() is None:
                for record in (OUT / 'rk3588_can.log').read_text().splitlines():
                    fields = record.split()
                    if len(fields) < 5:
                        continue
                    ident = int(fields[2], 16)
                    payload = bytes.fromhex(''.join(fields[4:]))
                    if ident == 0x601 and payload.hex() == '23ff600300000500' and target_seen is None:
                        target_seen = time.monotonic()
                    if target_seen is not None and ident == 0x181 and len(payload) == 8:
                        assert payload[4:6] == bytes(2), 'uncommanded-wheel velocity'
                        moving_seen = moving_seen or int.from_bytes(payload[6:], 'little', signed=True) > 0
                if target_seen is not None and time.monotonic() - target_seen >= 0.3 and not (OUT / 'armed.json').exists():
                    assert moving_seen, 'nonzero motion feedback absent before interface loss'
                    (OUT / 'armed.json.tmp').write_text(json.dumps({'pid':child.pid,'elf_sha256':EXPECTED,'moving_tpdo_observed':True,'wall_time':time.time()}))
                    (OUT / 'armed.json.tmp').replace(OUT / 'armed.json')
                    print('INTERFACE_ARMED',flush=True)
                if (gate / 'event.json').exists():
                    event = json.loads((gate / 'event.json').read_text())
                    assert event['state'] != 'failed', event
                    if event['state'] in ('down','up') and not signal_sent:
                        signal_sent = True
                        print('INTERFACE_DOWN',flush=True)
                    if event['state'] == 'up' and not link_restored:
                        link_restored = True
                        print('INTERFACE_UP',flush=True)
                lines = (OUT / 'executor.log').read_text().splitlines()
                for line in lines[reported_lines:]:
                    assert not any(word in line.lower() for word in ('error', 'warning', 'failed', 'permission denied')), 'application diagnostic: ' + line
                    print('APP ' + line, flush=True)
                reported_lines = len(lines)
                assert time.monotonic() < end, 'executor deadline'
                assert cap.poll() is None, 'capture stopped during trial'
                assert (OUT / 'capture.stderr').read_text() in ('', 'can0: interface down\n'), 'unexpected capture warning'
                if select.select([sys.stdin], [], [], 0.02)[0]:
                    raise RuntimeError('local abort or control pipe closed: ' + sys.stdin.readline().strip())
            lines = (OUT / 'executor.log').read_text().splitlines()
            for line in lines[reported_lines:]:
                print('APP ' + line, flush=True)
            assert not any(any(word in line.lower() for word in ('error', 'warning', 'failed', 'permission denied')) for line in lines), 'application diagnostic at exit'
            (OUT / 'executor.rc').write_text(str(child.returncode))
            expected = [line for line in lines if line.startswith('qualification_complete node=1')]
            assert signal_sent and link_restored and child.returncode == 0 and len(expected) == 1, 'expected external loss/recovery absent'
            rc = 0
        counter_snapshot('counters_executor_exited')
        time.sleep(0.25)
        counter_snapshot('counters_before_capture_stop')
finally:
    if child is not None and child.poll() is None:
        child.send_signal(signal.SIGTERM)
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            print('POWER_CUT_REQUIRED: executor did not exit after SIGTERM', flush=True)
            child.kill()
            child.wait(timeout=2)
        (OUT / 'executor.rc').write_text(str(child.returncode))
    if cap is not None and cap.poll() is None:
        cap.send_signal(signal.SIGINT)
        try:
            cap.wait(timeout=2)
        except subprocess.TimeoutExpired:
            cap.kill()
            cap.wait(timeout=2)
            rc = 1
    if cap is not None:
        (OUT / 'capture.rc').write_text(str(cap.returncode))
        if cap.returncode != 0:
            rc = 1
    if (OUT / 'capture.stderr').read_text() not in ('', 'can0: interface down\n'):
        rc = 1
    for name in ('ready.json', 'event.json'):
        if (gate / name).exists():
            (OUT / ('privileged_' + name)).write_bytes((gate / name).read_bytes())
    post = subprocess.check_output(['ip', '-j', '-details', '-statistics', 'link', 'show', 'can0'], text=True)
    (OUT / 'can_postflight.json').write_text(post)
    (OUT / 'wrapper_result.json').write_text(json.dumps({'executor_started': started, 'wrapper_exit': rc, 'capture_stopped': cap is None or cap.poll() is not None, 'executor_stopped': child is None or child.poll() is not None}))
print('TRIAL_FINISHED rc=' + str(rc), flush=True)
sys.exit(rc)
