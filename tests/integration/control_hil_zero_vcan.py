#!/usr/bin/env python3
"""Run the actual zero-HIL executable against an independent vcan/PTY peer only."""
import json
from pathlib import Path
import runpy
import tempfile
import os
import pty
import select
import signal
import socket
import struct
import subprocess
import time


def trial(scenario):
    """Check zero enable, rejected targets, transient X1, signal exit and restoration."""
    values = {
        (0x6040, 0): 0, (0x6041, 0): 0x00400040,
        (0x6060, 0): 3, (0x6061, 0): 3, (0x603F, 0): 0,
        (0x60FF, 1): 0, (0x60FF, 2): 0, (0x606C, 1): 0,
        (0x606C, 2): 0, (0x606C, 3): 0, (0x200F, 0): 1,
        (0x2000, 0): 0, (0x1017, 0): 0,
        (0x1400, 1): 0x201, (0x1400, 2): 255, (0x1400, 5): 1000,
        (0x1600, 0): 2, (0x1600, 1): 0x60400010, (0x1600, 2): 0x60600008,
        (0x1800, 1): 0x181, (0x1800, 2): 255, (0x1800, 5): 100,
        (0x1A00, 0): 2, (0x1A00, 1): 0x60410020, (0x1A00, 2): 0x606C0320,
        (0x1801, 1): 0x281, (0x1801, 2): 255, (0x1801, 5): 0,
        (0x1A01, 0): 0, (0x1A01, 1): 0, (0x1A01, 2): 0,
    }
    baseline = values.copy()
    bus = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    bus.bind(('vcan0',))
    bus.setblocking(False)
    master, slave = pty.openpty()
    os.set_blocking(master, False)
    program = os.environ['CONTROL_HIL_PROGRAM']
    process = subprocess.Popen([program, '--interface', 'vcan0', '--device', os.ttyname(slave),
                                '--duration-ms', '2500', '--zero-only'],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    os.set_blocking(process.stdout.fileno(), False)
    output = bytearray()
    control_started = None
    enabled_at = None
    injected = False
    words = []
    trace = []
    nmt = 127
    next_feedback = next_serial = time.monotonic()
    deadline = next_feedback + 12

    def send(identifier, data):
        """Serialize a classic CAN frame independently of project codecs."""
        trace.append((time.monotonic(), identifier, data))
        bus.send(struct.pack('=IB3x8s', identifier, len(data), data.ljust(8, b'\0')))

    def width(index, sub):
        """Peer dictionary widths, independent of the production allowlist."""
        if index in (0x6040, 0x1017, 0x2000, 0x200F) or (index in (0x1400, 0x1800, 0x1801) and sub == 5):
            return 2
        if index in (0x6060, 0x6061) or (index in (0x1400, 0x1800, 0x1801) and sub == 2) or (index in (0x1600, 0x1A00, 0x1A01) and sub == 0):
            return 1
        return 4

    try:
        while process.poll() is None:
            now = time.monotonic()
            assert now < deadline, 'bounded executable did not exit'
            readable, _, _ = select.select([bus, process.stdout], [], [], 0.001)
            if process.stdout in readable:
                output.extend(os.read(process.stdout.fileno(), 65536))
                if control_started is None and b'event=control_start' in output:
                    control_started = now
            if bus in readable:
                identifier, dlc, data = struct.unpack('=IB3x8s', bus.recv(16))
                data = data[:dlc]
                trace.append((time.monotonic(), identifier, data))
                if identifier == 0:
                    assert data in (b'\x80\x01', b'\x01\x01')
                    nmt = 5 if data[0] == 1 else 127
                elif identifier == 0x201:
                    assert dlc == 6 and data[1:] == bytes(5), 'nonzero target reached bus'
                    word = data[0]
                    words.append(word)
                    previous = values[0x6041, 0] & 0x6F
                    if word == 0:
                        state = 0x40
                    elif word == 2:
                        state = 0x07
                    elif word == 6:
                        state = 0x07 if previous == 0x07 else 0x21
                    elif word == 7:
                        assert previous in (0x21, 0x23)
                        state = 0x23
                    elif word == 15:
                        assert previous in (0x23, 0x27)
                        state = 0x27
                        enabled_at = enabled_at or now
                    else:
                        raise AssertionError(f'unknown controlword {word}')
                    values[0x6041, 0] = state | (state << 16)
                else:
                    assert identifier == 0x601 and dlc == 8
                    command, index, sub = struct.unpack('<BHB', data[:4])
                    assert (index, sub) in values
                    size = width(index, sub)
                    if command == 0x40:
                        response = bytes([0x43 + ((4-size) << 2)]) + data[1:4] + values[index, sub].to_bytes(4, 'little')
                    else:
                        assert command == {1: 0x2F, 2: 0x2B, 4: 0x23}[size]
                        value = int.from_bytes(data[4:], 'little')
                        if index in (0x1400, 0x1600, 0x1801, 0x1A01):
                            assert nmt == 127
                        if index == 0x6040:
                            assert value in (0, 6), 'bootstrap must not enable'
                            values[0x6041, 0] = 0x00400040 if value == 0 else 0x00210021
                        if index == 0x60FF:
                            assert value == 0
                        values[index, sub] = value
                        response = b'\x60' + data[1:4] + bytes(4)
                    send(0x581, response)
            if now >= next_feedback:
                next_feedback = now + 0.02
                if values[0x1017, 0]:
                    send(0x701, bytes([nmt]))
                if nmt == 5:
                    send(0x181, struct.pack('<II', values[0x6041, 0], 0))
                    if values[0x1A01, 0] == 2 and scenario != 'missing_diagnostics':
                        send(0x281, b'\x03'+bytes(4))
            if control_started and now >= next_serial:
                next_serial = now + 0.007
                channels = [1000]*16
                channels[2] = 993
                channels[5] = 1800 if now-control_started > 0.5 else 200
                if scenario == 'nonzero' and enabled_at and now-enabled_at > 0.2:
                    channels[2] = 1800
                    injected = True
                bits = sum(value << (11*i) for i, value in enumerate(channels))
                os.write(master, b'\x0f'+bits.to_bytes(22, 'little')+bytes(2))
            if enabled_at and now-enabled_at > 0.2 and not injected:
                if scenario == 'x1_pulse':
                    send(0x181, struct.pack('<II', values[0x6041, 0] | 0x8000, 0))
                    send(0x181, struct.pack('<II', values[0x6041, 0], 0))
                    injected = True
                elif scenario == 'sigterm':
                    process.send_signal(signal.SIGTERM)
                    injected = True
        output.extend(process.stdout.read() or b'')
        text = output.decode(errors='replace')
        assert process.returncode == (0 if scenario == 'happy' else 1), (
            text + '\nLAST CAN FRAMES:\n' + '\n'.join(
                f'{stamp:.6f} {ident:03x} {payload.hex()}' for stamp,ident,payload in trace[-60:]))
        assert 'restore_ok=1' in text, text
        assert values == baseline, (scenario, values, text)
        if scenario != 'missing_diagnostics':
            assert enabled_at and 0 in words and 6 in words and 7 in words and 15 in words, text
        if scenario in ('nonzero', 'x1_pulse', 'sigterm'):
            assert injected
        if scenario == 'nonzero':
            assert 'runtime_rpdo_send' in text or 'control_hil_zero_envelope' in text, text
        if scenario == 'happy':
            check_analyzer(trace, text, process.returncode)
        print(f'{scenario}: passed, zero_rpdo={len(words)}, restored=verified')
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        process.stdout.close()
        bus.close()
        os.close(master)
        os.close(slave)



def check_analyzer(trace, application_log, returncode):
    """Replay virtual evidence through the physical-capture oracle and reject tampering."""
    script = Path(__file__).resolve().parents[2] / 'docs/verification/evidence/p10_3_control_zero_20260916/analyze-zero.py'
    namespace = runpy.run_path(str(script))
    with tempfile.TemporaryDirectory(prefix='control-hil-oracle-') as directory:
        base = Path(directory)
        (base/'zero-target').mkdir()
        (base/'zero-jcan-once').mkdir()
        (base/'zero-target/application.log').write_text(application_log)
        (base/'zero-target/result.json').write_text(json.dumps({'passed':returncode == 0, 'application_exit':returncode}))
        namespace['main'].__globals__['BASE'] = base
        for mutation in ('none', 'target', 'restore'):
            rows = list(trace)
            if mutation == 'target':
                i = next(i for i, (_, ident, _) in enumerate(rows) if ident == 0x201)
                stamp, ident, payload = rows[i]
                rows[i] = (stamp, ident, payload[:2]+bytes([1])+payload[3:])
            elif mutation == 'restore':
                i = next(i for i, (_, ident, payload) in enumerate(rows)
                         if ident == 0x601 and payload == bytes.fromhex('2300160208006060'))
                stamp, ident, payload = rows[i]
                rows[i] = (stamp, ident, payload[:4]+bytes.fromhex('2003ff60'))
            (base/'zero-target/candump.log').write_text(''.join(
                f'({stamp:.6f}) can0 {ident:03X} [{len(payload)}] {payload.hex(" ")}' + chr(10)
                for stamp, ident, payload in rows))
            (base/'zero-jcan-once/session.jsonl').write_text(''.join(
                json.dumps({'event':'frame','can_id':ident,'data_hex':payload.hex(),
                            'brs':False,'extended':False,'fd':False,'remote':False})+chr(10)
                for _,ident,payload in rows))
            try:
                namespace['main']()
            except AssertionError:
                assert mutation != 'none'
            else:
                assert mutation == 'none', 'tampered capture passed'

if __name__ == '__main__':
    if os.environ.get('ROBOT_CONTROL_TEST_VCAN_INTERFACE') != 'vcan0':
        raise SystemExit(77)
    for case in ('happy', 'nonzero', 'x1_pulse', 'sigterm', 'missing_diagnostics'):
        trial(case)
