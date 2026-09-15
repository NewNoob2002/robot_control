#!/usr/bin/env python3
"""Run the single authorized synchronous packed-target trial with bounded passive capture."""
import hashlib, json, os, select, signal, subprocess, sys, time
from pathlib import Path
from trial_checks import parse_capture, check_diagnostic, completed_lines, classify_outcome
BASE = Path('/tmp/robot-control-qualifications/cable-repair-acc9f8828f65')
ELF = BASE / 'robot-control-zlac-qualification'
EXPECTED = 'acc9f8828f657d968cb2c56a586b1f9e1f2c50250f6c1385d3e6b7439abed46f'
authorization = json.loads((BASE / 'authorization_cable_loss_once.json').read_text())
assert authorization['stimulus'] == 'cable_loss'
assert authorization['args'] == ['--interface', 'can0', '--external-loss-once']
assert authorization['authorized'] is True and authorization['elf_sha256'] == EXPECTED
assert authorization['operator_ready'] is True
assert hashlib.sha256(ELF.read_bytes()).hexdigest() == EXPECTED
assert Path('/etc/machine-id').read_text().strip() == '6923ab3301fb4a8d816759b04ec6bf0a'
ready = json.loads((BASE / 'operator_ready.json').read_text())
assert 0 <= time.time() - ready['wall_time'] <= 180 and ready['elf_sha256'] == EXPECTED
os.kill(ready['pid'], 0)
for p in Path('/proc').iterdir():
    if not p.name.isdigit():
        continue
    try:
        name = (p / 'exe').resolve(strict=True).name
        assert name not in ('cansend', 'cangen', 'robot-control-zlac-qualification', 'robot-control-canopen-commission'), (p.name, name)
    except (FileNotFoundError, PermissionError, ProcessLookupError):
        pass
OUT = BASE / 'cable_loss_once'
OUT.mkdir(exist_ok=False)
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
target_seen = None
moving_seen = False
processed_frames = 0
error_seen = False
post_error_requests = 0
outcome = None
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
                records = parse_capture((OUT / 'rk3588_can.log').read_text())
                for ident, payload in records[processed_frames:]:
                    if ident & 0x20000000:
                        error_seen = True
                        continue
                    if error_seen and ident in (0, 0x601, 0x201):
                        post_error_requests += 1
                    if ident == 0x601 and payload.hex() == '23ff600300000500' and target_seen is None:
                        target_seen = time.monotonic()
                    if target_seen is not None and ident == 0x181 and len(payload) == 8:
                        assert payload[4:6] == bytes(2), 'uncommanded-wheel velocity'
                        moving_seen = moving_seen or int.from_bytes(payload[6:], 'little', signed=True) > 0
                processed_frames = len(records)
                if target_seen is not None and time.monotonic() - target_seen >= 0.3 and not (OUT / 'armed.json').exists():
                    assert moving_seen, 'nonzero motion feedback absent before cable loss'
                    (OUT / 'armed.json.tmp').write_text(json.dumps({'pid':child.pid,'elf_sha256':EXPECTED,'moving_tpdo_observed':True,'wall_time':time.time()}))
                    (OUT / 'armed.json.tmp').replace(OUT / 'armed.json')
                    print('MANUAL_DISCONNECT_ARMED',flush=True)
                lines = completed_lines((OUT / 'executor.log').read_text())
                for line in lines[reported_lines:]:
                    check_diagnostic(line)
                    print('APP ' + line, flush=True)
                reported_lines = len(lines)
                assert time.monotonic() < end, 'executor deadline'
                assert cap.poll() is None, 'capture stopped during trial'
                assert (OUT / 'capture.stderr').read_text() == '', 'unexpected capture warning'
                if select.select([sys.stdin], [], [], 0.02)[0]:
                    raise RuntimeError('local abort or control pipe closed: ' + sys.stdin.readline().strip())
            lines = (OUT / 'executor.log').read_text().splitlines()
            for line in lines[reported_lines:]:
                print('APP ' + line, flush=True)
            for line in lines:
                check_diagnostic(line)
            (OUT / 'executor.rc').write_text(str(child.returncode))
            outcome = classify_outcome(child.returncode, lines)
            (OUT / 'outcome.json').write_text(json.dumps({'outcome': outcome, 'physical_acceptance': 'PENDING_OPERATOR'}))
            print('OUTCOME ' + outcome, flush=True)
        counter_snapshot('counters_executor_exited')
        # Preserve evidence through the agreed stationary reconnect observation.
        observe_until = time.monotonic() + 10
        while time.monotonic() < observe_until:
            assert cap.poll() is None, 'capture ended during reconnect observation'
            assert not (OUT / 'capture.stderr').read_text(), 'capture diagnostic'
            time.sleep(0.05)
        records = parse_capture((OUT / 'rk3588_can.log').read_text())
        error_seen = False
        post_error_requests = 0
        for ident, payload in records:
            if ident & 0x20000000:
                error_seen = True
            elif error_seen and ident in (0, 0x601, 0x201):
                post_error_requests += 1
        assert post_error_requests == 0, 'host request transmitted after CAN error'
        assert outcome != 'INHIBITED_POWER_OFF_REQUIRED' or error_seen, 'missing raw CAN error evidence'
        counter_snapshot('counters_before_capture_stop')
        rc = 0
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
    if (OUT / 'capture.stderr').read_text() != '':
        rc = 1
    post = subprocess.check_output(['ip', '-j', '-details', '-statistics', 'link', 'show', 'can0'], text=True)
    (OUT / 'can_postflight.json').write_text(post)
    (OUT / 'wrapper_result.json').write_text(json.dumps({'outcome': outcome, 'raw_can_error': error_seen, 'post_error_requests': post_error_requests, 'executor_started': started, 'wrapper_exit': rc, 'capture_stopped': cap is None or cap.poll() is not None, 'executor_stopped': child is None or child.poll() is not None}))
print('TRIAL_FINISHED rc=' + str(rc), flush=True)
sys.exit(rc)
