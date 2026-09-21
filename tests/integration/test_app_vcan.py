#!/usr/bin/env python3
"""Exercise the real SBUS app against a separately implemented virtual CANopen peer. No physical interface."""
from pathlib import Path
import os
import fcntl
import pty
import select
import signal
import socket
import struct
import subprocess
import tempfile
import time


def frame(steering=1000, throttle=993, button=200, flags=0, gear=1800):
    """Encode independent standard 25-byte SBUS input vectors."""
    channels = [1000] * 16
    channels[0], channels[2], channels[5], channels[6] = steering, throttle, button, gear
    bits = sum(value << (11 * index) for index, value in enumerate(channels))
    return b'\x0f' + bits.to_bytes(22, 'little') + bytes([flags, 0])


def device_baseline(resolution):
    """Return independent normal-baseline OD values shared by app/provisioning peers."""
    return {
        (0x2007, 0): 0, (0x2008, 0): 100, (0x2026,3): 0, (0x2026,4): 0, (0x2026,7): 0,
        (0x6083,1): 500, (0x6083,2): 500, (0x6084,1): 500, (0x6084,2): 500,
        (0x6085,1): 10, (0x6085,2): 10, (0x2026, 5): resolution, (0x6040, 0): 0, (0x6041, 0): 0x00400040, (0x6060, 0): 3, (0x6061, 0): 3,
        (0x603F, 0): 0, (0x605A, 0): 5, (0x60FF, 1): 0, (0x60FF, 2): 0,
        (0x606C, 1): 0, (0x606C, 2): 0, (0x606C, 3): 0, (0x200F, 0): 1,
        (0x2000, 0): 0, (0x1017, 0): 0, (0x1400, 1): 0x201, (0x1400, 2): 255,
        (0x1400, 5): 1000, (0x1600, 0): 2, (0x1600, 1): 0x60400010,
        (0x1600, 2): 0x60600008, (0x1800, 1): 0x181, (0x1800, 2): 255,
        (0x1800, 5): 100, (0x1A00, 0): 2, (0x1A00, 1): 0x60410020,
        (0x1A00, 2): 0x606C0320, (0x1801, 1): 0x281, (0x1801, 2): 255,
        (0x1801, 5): 0, (0x1A01, 0): 0, (0x1A01, 1): 0, (0x1A01, 2): 0,
    }


def trial(scenario):
    """Drive the peer dictionary, actual RPDOs, SBUS phases and independent protection assertions."""
    maximum = 25 if scenario == 'gears' else 5
    resolution = 10 if scenario in ('resolution10','gears') else 2 if scenario == 'resolution2' else 1
    setup_failure = scenario == 'device_mismatch' or scenario in ('resolution_abort', 'resolution_mismatch', 'resolution_invalid', 'resolution_timeout')
    interface = 'vcan1' if scenario == 'interface' else 'vcan0'
    assert interface.startswith('vcan')
    values = device_baseline(resolution)
    if scenario == 'device_mismatch': values[0x6083,1] = 700
    if scenario == 'baseline': values[0x200F, 0] = 0
    if scenario == 'resolution_invalid': values[0x2026,5] = 0
    if scenario == 'resolution_mismatch': values[0x2026,5] = 4
    baseline = values.copy()
    bus = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    bus.bind((interface,)); bus.setblocking(False)
    master, slave = pty.openpty(); os.set_blocking(master, False)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        env = dict(os.environ, ROBOT_CONTROL_TEST_VCAN_INTERFACE=interface)
        command = [os.environ['TEST_APP_PROGRAM'], '--config', os.environ['TEST_APP_CONFIG'],
                   '--interface', interface, '--device', os.ttyname(slave), '--log-dir', str(root),
                   '--duration-ms', '7500' if scenario == 'drive' else '0' if scenario in ('sigterm','sighup') else '4000',
                   '--set', f'max_rpm={maximum}', '--set', f'speed_resolution={resolution}', '--set', 'log_file_bytes=65536', '--set', 'log_files=3']
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
        os.set_blocking(process.stdout.fileno(), False)
        console_start = time.monotonic()
        if scenario == 'console_slow': fcntl.fcntl(process.stdout, fcntl.F_SETPIPE_SZ, 4096)
        output = bytearray(); sent = []; writes = []
        nmt = 127; targets = (0, 0); speeds = (0, 0)
        started = enabled = first_motion = injected = None
        stopped_after_injection = None; child = None; second = None
        next_serial = next_feedback = time.monotonic(); deadline = next_serial + 16

        def send(identifier, payload):
            """Use Linux's wire frame directly; this peer imports no project encoders or guards."""
            bus.send(struct.pack('=IB3x8s', identifier, len(payload), payload.ljust(8, b'\0')))

        def apply_word(word):
            """Model only the transitions exercised by the app, without automatic fault reset."""
            nonlocal speeds, targets, enabled
            states = {0: 0x40, 2: 7, 6: 0x21, 7: 0x23, 15: 0x27}
            assert word in states, word
            values[0x6040, 0] = word
            values[0x6041, 0] = states[word] * 0x10001
            if word != 15: targets = speeds = (0, 0)
            elif enabled is None: enabled = time.monotonic()

        try:
            while time.monotonic() < deadline:
                now = time.monotonic()
                for ready in select.select([bus] if scenario == 'console_slow' and now-console_start < 2 else [bus, process.stdout], [], [], 0.002)[0]:
                    if ready is process.stdout:
                        output.extend(os.read(process.stdout.fileno(), 65536))
                        continue
                    while True:
                        try: raw = bus.recv(16)
                        except BlockingIOError: break
                        identifier, size, payload = struct.unpack('=IB3x8s', raw)
                        payload = payload[:size]
                        if identifier == 0:
                            assert payload in (b'\x01\x01', b'\x80\x01'), payload
                            nmt = 5 if payload[0] == 1 else 127
                        elif identifier == 0x601:
                            command_byte = payload[0]
                            index = int.from_bytes(payload[1:3], 'little'); sub = payload[3]
                            key = index, sub
                            assert key in values, key
                            width = 2 if index in (0x2007,0x2008,0x2026,0x6040,0x605A,0x1017,0x2000,0x200F) or (index in (0x1400,0x1800,0x1801) and sub == 5) else 1 if index in (0x6060,0x6061) or (index in (0x1400,0x1800,0x1801) and sub == 2) or (index in (0x1600,0x1A00,0x1A01) and sub == 0) else 4
                            if command_byte == 0x40:
                                if key == (0x2026,5) and scenario == 'resolution_abort':
                                    send(0x581, bytes([0x80]) + payload[1:4] + struct.pack('<I',0x06090030))
                                    continue
                                if key == (0x2026,5) and scenario == 'resolution_timeout':
                                    continue
                                send(0x581, bytes([{1:0x4F,2:0x4B,4:0x43}[width]]) + payload[1:4] + values[key].to_bytes(4, 'little'))
                            else:
                                assert command_byte == {1:0x2F,2:0x2B,4:0x23}[width]
                                value = int.from_bytes(payload[4:4+width], 'little')
                                writes.append((key,value))
                                assert key[0] not in (0x2026,0x2007,0x2008,0x6083,0x6084,0x6085,0x6060,0x200F,0x605A), 'runtime must never write persistent parameters'
                                values[key] = value
                                if index == 0x6040: apply_word(value)
                                if index == 0x60FF: assert value == 0
                                send(0x581, b'\x60' + payload[1:4] + bytes(4))
                        elif identifier == 0x201:
                            word, left, right = struct.unpack('<Hhh', payload)
                            assert -maximum*resolution <= left <= maximum*resolution and -maximum*resolution <= right <= maximum*resolution
                            assert left % resolution == 0 and right % resolution == 0
                            assert (left == right == 0) or word == 15
                            apply_word(word); targets = (left,right); speeds = (left*10//values[0x2026,5],right*10//values[0x2026,5])
                            sent.append((now,word,left,right))
                            if (left or right) and first_motion is None: first_motion = now
                            if injected and left == right == 0 and stopped_after_injection is None:
                                stopped_after_injection = now
                runs = list(root.glob('run-*'))
                if runs and started is None:
                    application = runs[0] / 'application.log'
                    if application.exists() and 'event=control_start ' in application.read_text(): started = now
                elapsed = now - first_motion if first_motion else 0
                steering, throttle, button, flags = 1000, 993, 200, 0
                if started and now - started > .2: button = 1800
                if started and now - started > .8:
                    throttle = 1800
                    if scenario == 'drive' and first_motion:
                        if elapsed < 3.3: pass
                        elif elapsed < 3.6: throttle = 993
                        elif elapsed < 4.1: throttle = 200
                        elif elapsed < 4.4: throttle = 993
                        elif elapsed < 4.9: steering,throttle = 200,993
                        elif elapsed < 5.2: throttle = 993
                        elif elapsed < 5.7: steering,throttle = 1800,993
                        else: throttle = 993
                    if scenario == 'ch6' and first_motion:
                        if .4 < elapsed < .7: button,throttle = 200,993
                        elif .7 <= elapsed < 1.0: button,throttle = 1800,993
                        elif 1.0 <= elapsed < 1.3: button,throttle = 200,993
                        elif 1.3 <= elapsed < 1.8: button,throttle = 1800,993
                if first_motion and elapsed >= .4 and injected is None and scenario in (
                    'sigterm','sighup','sbus_loss','logger_exit','logger_stall','overspeed','can_loss','second'):
                    injected = now
                    if scenario in ('sigterm','sighup'): process.send_signal(signal.SIGTERM if scenario == 'sigterm' else signal.SIGHUP)
                    if scenario.startswith('logger_'):
                        children = Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().split()
                        assert len(children) == 1
                        child = int(children[0]); os.kill(child, signal.SIGKILL if scenario == 'logger_exit' else signal.SIGSTOP)
                    if scenario == 'second':
                        second = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=5)
                        assert second.returncode == 1
                        injected = None  # This is not a stop stimulus for the first app.
                        scenario = 'second_done'
                if injected and scenario == 'sbus_loss': flags = 4
                if scenario == 'standby': button,throttle = 200,993
                if now >= next_serial:
                    gear = 1800
                    if scenario == 'gears':
                        gear = 500 if elapsed < .7 else 501 if elapsed < 1.4 else 1499 if elapsed < 2.1 else 1500
                    if scenario != 'remote_off':
                        os.write(master, frame(steering, throttle, button, flags, gear))
                    next_serial = now + .007
                if now >= next_feedback:
                    next_feedback = now + .02
                    if not (injected and scenario == 'can_loss' and now-injected < .45):
                        if values[0x1017,0]: send(0x701, bytes([nmt]))
                        if nmt == 5:
                            observed = (1001, speeds[1]) if injected and scenario == 'overspeed' and any(targets) else speeds
                            values[0x606C,1] = observed[0] & 0xFFFFFFFF
                            values[0x606C,2] = observed[1] & 0xFFFFFFFF
                            values[0x606C,3] = (observed[0] & 0xFFFF) | ((observed[1] & 0xFFFF) << 16)
                            send(0x181, struct.pack('<Ihh', values[0x6041,0], *observed))
                            if values[0x1A01,0] == 2: send(0x281, bytes([3]) + struct.pack('<I', values[0x603F,0]))
                    else:
                        # SDO remains available for cleanup; this case loses periodic feedback only.
                        values[0x606C,1],values[0x606C,2],values[0x606C,3] = 0,0,0
                if process.poll() is not None: break
            assert process.poll() is not None, ('app timeout',scenario,bytes(output))
            output.extend(process.stdout.read() or b'')
            expected = 143 if scenario in ('sigterm','sighup') else 1 if setup_failure or scenario in ('baseline','logger_exit','logger_stall','overspeed','can_loss') else 0
            assert process.returncode == expected, (scenario,process.returncode,bytes(output), [p.read_text()[-5000:] for p in root.glob('run-*/application.log')])
            if scenario in ('baseline','resolution_invalid'): assert not writes and not sent
            elif setup_failure:
                assert not first_motion and not sent
                assert values[0x2026,5] == baseline[0x2026,5]
                assert values[0x1017,0] == baseline[0x1017,0]
            else:
                assert sent[-1][2:] == (0,0), (scenario,sent[-8:])
                if scenario in ('standby', 'remote_off'):
                    assert not first_motion and enabled is not None
                    held = [(word,left,right) for stamp,word,left,right in sent if started+.8 < stamp < started+3]
                    assert held and all(item == (15,0,0) for item in held), (scenario,held)
                else:
                    assert first_motion, (scenario, [p.read_text() for p in root.glob('run-*/diagnostics.log')])
                for key,value in baseline.items():
                    if key not in ((0x6040,0),(0x6041,0),(0x606C,1),(0x606C,2),(0x606C,3)):
                        assert values[key] == value, (scenario,key,values[key],value)
                if scenario in ('resolution2','resolution10'):
                    assert any(abs(left) == 5*resolution for _,_,left,_ in sent)
                if injected:
                    assert stopped_after_injection is not None
                    assert stopped_after_injection - injected < (.7 if scenario.startswith('logger_') else .35), scenario
                    assert not any(left or right for stamp,word,left,right in sent if stamp > stopped_after_injection), scenario
                if scenario == 'drive':
                    assert any(stamp-first_motion > 3 and left==5 and right==-5 for stamp,word,left,right in sent)
                    assert {(-5,5),(-5,-5),(5,5)} <= {(left,right) for _,_,left,right in sent}
                if scenario == 'gears':
                    assert {(70,-70),(150,-150),(250,-250)} <= {(left,right) for _,_,left,right in sent}
                if scenario == 'ch6':
                    assert any(stamp-first_motion > 2 and (left or right) for stamp,_,left,right in sent), ([p.read_text() for p in root.glob('run-*/diagnostics.log')], sent[-12:])
                    assert all(word == 15 for stamp,word,_,_ in sent if first_motion < stamp < first_motion+2), sent
                if scenario == 'sbus_loss':
                    held = [(word,left,right) for stamp,word,left,right in sent if injected+.35 < stamp < started+3]
                    assert held and all(item == (15,0,0) for item in held), held
            if not scenario.startswith('logger_'):
                assert b'I/app' in output and b'I/vehicle' in output, bytes(output)
                assert b'pid:' in output and b'tid:' in output
                app_files = list(root.glob('run-*/application.log*'))
                app_text = '\n'.join(p.read_text() for p in app_files)
                assert 'hil_read index=0x6041 sub=0x00 raw=0x00400040' in app_text, (scenario, app_text)
                assert 'hil_read index=24641' not in app_text
                assert all(all(line.startswith('I/app') for line in p.read_text().splitlines()) for p in app_files)
                assert b'event=app_stop' in output or scenario in ('baseline','resolution_invalid')
                if scenario == 'resolution_restore':
                    assert b'cleanup_unverified' in output

                assert b'logs_complete=1' in output, (scenario,bytes(output))
                run = sorted(root.glob('run-*'))[0]
                segments = sorted(run.glob('trace.bin.*'), key=lambda p:int(p.suffix[1:]), reverse=True) + [run/'trace.bin']
                assert len(segments) <= 3
                packets = []
                for path in segments:
                    raw = path.read_bytes(); assert len(raw) <= 65536 and len(raw)%256 == 0
                    packets.extend(struct.iter_unpack('<32q',raw))
                assert packets and all(p[0] == 0x31414352 for p in packets)
                assert all(b[1] == a[1]+1 for a,b in zip(packets,packets[1:]))
                assert packets[-1][2] == 7
                diagnostic = (run/'diagnostics.log').read_text()
                assert 'logger_end' in diagnostic
                if scenario == 'console_slow':
                    import re
                    assert int(re.search(r'dropped_records=(\d+)', diagnostic)[1]) > 0
                else:
                    for p in (run/'application.log',run/'diagnostics.log'):
                        for line in p.read_bytes().splitlines():
                            assert line in output, (scenario,line)

            print('PASS',scenario,'RPDOs',len(sent),'writes',len(writes),flush=True)
        finally:
            if child:
                try: os.kill(child, signal.SIGCONT)
                except ProcessLookupError: pass
            if process.poll() is None:
                process.kill(); process.wait(timeout=3)
            os.close(master); os.close(slave); bus.close()


def main():
    """Run only inside the managed namespace; create a second virtual link to prove interface selection."""
    assert os.environ.get('ROBOT_CONTROL_TEST_VCAN_INTERFACE') == 'vcan0'
    subprocess.run(['ip','link','add','dev','vcan1','type','vcan'],check=True)
    subprocess.run(['ip','link','set','dev','vcan1','up'],check=True)
    scenarios = os.environ.get('TEST_APP_CASES', 'baseline,device_mismatch,gears,console_slow,resolution_invalid,resolution_timeout,resolution_abort,resolution_mismatch,resolution2,resolution10,interface,drive,ch6,standby,remote_off,sigterm,sighup,sbus_loss,overspeed,can_loss,logger_exit,logger_stall,second').split(',')
    for scenario in scenarios: trial(scenario)


if __name__ == '__main__':
    main()
