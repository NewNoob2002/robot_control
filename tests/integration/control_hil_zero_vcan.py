#!/usr/bin/env python3
"""Run the actual zero-HIL executable against an independent vcan/PTY peer only."""
import fcntl
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
    startup = scenario.startswith("motion_throttle_startup_")
    startup_timeout = scenario == "motion_throttle_startup_timeout"
    standstill_noise = scenario in ("motion_throttle_tolerance", "motion_throttle_tolerance_reject")
    extended = scenario in ("motion_throttle_long_failsafe", "motion_throttle_long_deadline")
    motion_limit = 8.0 if extended else 3.0
    restoring = scenario.startswith("restore_")
    motion = scenario.startswith("motion_")
    recovery = scenario.startswith("recovery_")
    uart = scenario.startswith("recovery_uart_")
    partial_injected = False
    resync_injected = 0
    uart_ready_since = None
    uart_ready_mark = -1
    x1_recovery = scenario in ('recovery_native_x1', 'recovery_x1', 'recovery_missing', 'recovery_fault', 'recovery_nonzero', 'recovery_no_rearm')
    tolerant = scenario in ('recovery_jitter', 'recovery_jitter_reject')
    x1_recovery = x1_recovery or tolerant
    input_case = scenario.startswith("input_")
    throttle_only = scenario.startswith("motion_throttle")
    right = scenario == "motion_right" or throttle_only
    success = scenario in ("happy", "motion_left", "motion_right", "motion_deadline", "motion_late_authorization", "input_observation")
    success = success or scenario in ('restore_baseline', 'motion_throttle', 'motion_throttle_tolerance', 'motion_throttle_tolerance_reject', 'motion_stop_rebound', 'motion_throttle_long_deadline')
    success = success or scenario in ('recovery_native_x1', 'recovery_x1', 'recovery_failsafe', 'recovery_silence', 'recovery_jitter', 'recovery_uart_partial', 'recovery_uart_clean', 'recovery_uart_resync', 'recovery_uart_bounce', 'recovery_uart_bounce_after_ready')
    success = success or (startup and not startup_timeout)
    values = {
        (0x6040, 0): 0, (0x6041, 0): 0x00400040,
        (0x6060, 0): 3, (0x6061, 0): 3, (0x603F, 0): 0,
        (0x605A, 0): 0 if scenario == 'recovery_option' else 5,
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
    if restoring:
        values.update({(0x2000,0):1000, (0x1017,0):500, (0x1600,2):0x60ff0320,
                       (0x1801,5):100, (0x1A01,0):2, (0x1A01,1):0x60610008, (0x1A01,2):0x603f0020})
        if scenario == 'restore_foreign': values[0x1600,2] = 0x12345678
    initial_values = values.copy()
    bus = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    bus.bind(('vcan0',))
    bus.setblocking(False)
    master, slave = pty.openpty()
    os.set_blocking(master, False)
    program = os.environ['CONTROL_HIL_PROGRAM']
    process = subprocess.Popen([program, '--interface', 'vcan0', '--device', os.ttyname(slave),
                                '--duration-ms', '60000' if scenario == 'recovery_uart_reconnect_timeout' else '15000' if uart else '2500' if startup_timeout else '16000' if extended else '11000' if recovery else ('60000' if scenario == 'motion_late_authorization' else '6000') if motion else '2500',
                                '--restore-zero-baseline' if restoring else ('--zero-uart-recovery' if uart else '--zero-x1-recovery' if x1_recovery else '--zero-sbus-recovery') if recovery else
                                ('--right-throttle' if throttle_only else '--single-right' if right else '--single-left') if motion else ('--observe-input' if input_case else '--zero-only')] + (['--zero-feedback-tenths-rpm', '10'] if tolerant or restoring else [] if standstill_noise or input_case else ['--zero-feedback-tenths-rpm', '0']) + (['--motion-window-ms', '8000'] if extended else []),
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    os.set_blocking(process.stdout.fileno(), False)
    if input_case:
        fcntl.fcntl(process.stdout.fileno(), fcntl.F_SETPIPE_SZ, 4096)
    feedback_rejected = False
    stop_drain = False
    output = bytearray()
    control_started = None
    motion_ready = None
    first_motion = None
    zero_requested = None
    targets = (0, 0)
    speeds = (0, 0)
    enabled_at = None
    injected = False
    fault_started = None
    recovery_phase = None
    recovery_since = None
    x1_active = False
    words = []
    trace = []
    nmt = 5 if restoring else 127
    next_feedback = next_serial = time.monotonic()
    deadline = next_feedback + (70 if scenario == 'recovery_uart_reconnect_timeout' else 20 if extended else 35 if scenario == 'motion_late_authorization' else 20 if uart else 16 if recovery else 12)

    def send(identifier, data):
        """Serialize a classic CAN frame independently of project codecs."""
        trace.append((time.monotonic(), identifier, data))
        bus.send(struct.pack('=IB3x8s', identifier, len(data), data.ljust(8, b'\0')))

    def width(index, sub):
        """Peer dictionary widths, independent of the production allowlist."""
        if index in (0x6040, 0x605A, 0x1017, 0x2000, 0x200F) or (index in (0x1400, 0x1800, 0x1801) and sub == 5):
            return 2
        if index in (0x6060, 0x6061) or (index in (0x1400, 0x1800, 0x1801) and sub == 2) or (index in (0x1600, 0x1A00, 0x1A01) and sub == 0):
            return 1
        return 4

    try:
        while process.poll() is None:
            now = time.monotonic()
            assert now < deadline, 'bounded executable did not exit'
            readable, _, _ = select.select([bus, process.stdout], [], [], 0.001)
            if process.stdout in readable and not stop_drain:
                output.extend(os.read(process.stdout.fileno(), 65536))
                if control_started is None and (b'event=control_start' in output or b'event=input_observation_ready' in output):
                    control_started = now
                if motion_ready is None and b'event=motion_ready' in output:
                    motion_ready = now
                if scenario == 'input_export_timeout' and b'event=trace_header' in output:
                    stop_drain = True
                if uart:
                    mark = output.rfind(b'event=uart_reconnected ')
                    if mark >= 0 and mark != uart_ready_mark:
                        uart_ready_mark, uart_ready_since = mark, now
                    if output.rfind(b'event=uart_resync ') > mark:
                        uart_ready_since = None
                if recovery:
                    phases = [phase for phase in ('fault_ready', 'release_fault', 'neutral_ready', 'rearm_ready', 'complete')
                              if ('event=recovery phase='+phase).encode() in output]
                    if phases and recovery_phase != phases[-1]:
                        recovery_phase, recovery_since = phases[-1], now
                    if recovery_phase == 'fault_ready' and scenario not in ('recovery_missing','recovery_nonzero') and fault_started is None:
                        fault_started = now
                        injected = True
                    x1_active = x1_recovery and fault_started is not None and recovery_phase == 'fault_ready'
            if bus in readable:
                identifier, dlc, data = struct.unpack('=IB3x8s', bus.recv(16))
                data = data[:dlc]
                trace.append((time.monotonic(), identifier, data))
                if identifier == 0:
                    assert data in (b'\x80\x01', b'\x01\x01')
                    nmt = 5 if data[0] == 1 else 127
                elif identifier == 0x201:
                    assert dlc == 6 and data[1] == 0
                    targets = struct.unpack('<hh', data[2:])
                    if motion:
                        selected, other = (targets[1], targets[0]) if right else targets
                        assert 0 <= selected <= 5 and other == 0, 'out-of-envelope target reached bus'
                        if any(targets):
                            assert motion_ready is not None and data[0] == 15
                            assert zero_requested is None, 'nonzero target restarted after stop'
                            first_motion = first_motion or now
                            assert now - first_motion < motion_limit + 0.05
                        elif first_motion is not None and zero_requested is None:
                            zero_requested = now
                    else:
                        assert targets == (0, 0), 'nonzero target reached zero-only bus'
                    word = data[0]
                    words.append(word)
                    previous = values[0x6041, 0] & 0x6F
                    if word == 0:
                        state = 0x40
                    elif word == 2:
                        state = 0x60 if scenario == 'recovery_native_x1' and previous != 0x27 else 0x07
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
            if first_motion and now-first_motion > 0.3 and not injected and scenario in ('motion_x1','motion_failsafe','motion_silence','motion_throttle_x1','motion_throttle_failsafe','motion_throttle_silence'):
                fault_started, injected = now, True
                x1_active = scenario in ('motion_x1','motion_throttle_x1')
            if scenario == 'motion_throttle_long_failsafe' and first_motion and now-first_motion > 4.0 and not injected:
                fault_started, injected = now, True
            if now >= next_feedback:
                next_feedback = now + 0.02
                if values[0x1017, 0]:
                    send(0x701, bytes([nmt]))
                if nmt == 5:
                    if motion:
                        if any(targets):
                            speeds = tuple(v*10 for v in targets)
                        elif zero_requested and now-zero_requested >= 0.15 and scenario != 'motion_stop_timeout':
                            speeds = (0, 0)
                        if scenario == 'motion_no_feedback': speeds = (0, 0)
                        if scenario == 'motion_wrong_feedback' and first_motion and zero_requested is None:
                            speeds = (speeds[0], 10)
                        if scenario == 'motion_stop_rebound' and zero_requested:
                            pattern = [-10,-36,9,-5,0,3,0,0,-6,-1,0,0,0,0]
                            position = min(int((now-zero_requested)/0.05), len(pattern)-1)
                            speeds = (pattern[position], 0)
                        values[0x606C, 1], values[0x606C, 2] = speeds
                        values[0x606C, 3] = (speeds[1] << 16) | speeds[0]
                    if standstill_noise and control_started:
                        if not first_motion or (zero_requested and now-zero_requested >= 0.15):
                            v = -21 if scenario == 'motion_throttle_tolerance_reject' and zero_requested and now-zero_requested < 0.30 else -20
                            speeds = (20, v)
                        else:
                            speeds = (20, speeds[1])
                        values[0x606C, 1], values[0x606C, 2] = (v & 0xffffffff for v in speeds)
                        values[0x606C, 3] = ((speeds[1] & 0xffff) << 16) | (speeds[0] & 0xffff)
                    if tolerant and control_started:
                        speeds = (10, -10) if int(now*50) % 2 else (-10, 10)
                        values[0x606C, 1], values[0x606C, 2] = (v & 0xffffffff for v in speeds)
                        values[0x606C, 3] = ((speeds[1] & 0xffff) << 16) | (speeds[0] & 0xffff)
                        if scenario == 'recovery_jitter_reject' and enabled_at and now-enabled_at > 0.1 and not feedback_rejected:
                            speeds = (11, 0)
                            feedback_rejected = True
                    send(0x181, struct.pack('<Ihh', values[0x6041, 0] | (0x8000 if x1_active else 0), *speeds))
                    if values[0x1A01, 0] == 2 and scenario != 'missing_diagnostics':
                        send(0x281, bytes([3,1 if scenario == 'recovery_fault' and injected else 0,0,0,0]))
            if control_started and now >= next_serial and not stop_drain:
                next_serial = now + 0.007
                channels = [1000]*16
                channels[2] = 993
                channels[5] = 1800 if now-control_started > (21 if scenario == 'motion_late_authorization' else 0.5) else 200
                if scenario == 'nonzero' and enabled_at and now-enabled_at > 0.2:
                    channels[2] = 1800
                    injected = True
                if motion_ready and now-motion_ready > 0.2:
                    channels[0] = 200 if right else 1800
                    channels[2] = 1800
                    if throttle_only: channels[0] = 1000
                    if scenario == 'motion_throttle_steering': channels[0] = 1800
                    if scenario == 'motion_throttle_reverse': channels[2] = 200
                    if scenario == 'motion_both': channels[0] = 1000
                    if scenario == 'motion_reverse': channels[2] = 200
                    if first_motion and now-first_motion > 0.8 and scenario not in ('motion_deadline', 'motion_throttle_long_deadline', 'motion_throttle_long_failsafe'):
                        channels[0], channels[2] = 1000, 993
                if input_case:
                    channels[5] = 200
                    elapsed = now-control_started
                    if 0.5 < elapsed < 0.85 or 1.3 < elapsed < 1.7:
                        channels[0], channels[2] = 1400, 1396
                flags = 0
                if scenario in ('motion_failsafe','motion_throttle_failsafe','motion_throttle_long_failsafe') and injected:
                    flags = 12
                if scenario in ('motion_silence','motion_throttle_silence') and injected:
                    continue
                if recovery:
                    channels[5] = 200 if now-control_started < 0.5 else 1800
                    if recovery_phase == 'fault_ready' and fault_started:
                        flags = 12 if scenario == 'recovery_failsafe' else 0
                        if uart or scenario == 'recovery_sbus_partial':
                            if scenario == 'recovery_uart_flags':
                                flags = 12
                            else:
                                if not partial_injected:
                                    if scenario == 'recovery_uart_service_gap':
                                        process.send_signal(signal.SIGSTOP)
                                        time.sleep(0.07)
                                        process.send_signal(signal.SIGCONT)
                                    elif scenario != 'recovery_uart_clean':
                                        os.write(master, bytes([15])+bytes(16) if scenario != 'recovery_uart_backlog' else bytes(512))
                                    partial_injected = True
                                elif scenario == 'recovery_uart_noise' and now-fault_started > 0.25:
                                    os.write(master, bytes([1]))
                                continue
                        if scenario == 'recovery_silence':
                            continue
                    elif recovery_phase == 'fault_ready' and scenario == 'recovery_nonzero':
                        channels[2] = 1800
                    elif recovery_phase == 'release_fault':
                        if scenario == 'recovery_uart_reconnect_timeout':
                            continue
                        if scenario == 'recovery_uart_resync' and not resync_injected:
                            os.write(master, bytes([15])+bytes(23)+bytes([1]))
                            resync_injected = True
                            continue
                        if scenario == 'recovery_uart_bounce' and resync_injected < 2 and now-recovery_since > (0.47 if resync_injected == 0 else 0.94):
                            os.write(master, bytes([15])+bytes(23)+bytes([1]))
                            resync_injected += 1
                            continue
                        if scenario == 'recovery_uart_bounce_after_ready' and uart_ready_since and not resync_injected and now-uart_ready_since > 0.25:
                            os.write(master, bytes([15])+bytes(23)+bytes([1]))
                            resync_injected += 1
                            continue
                        if uart:
                            channels[2] = 1800 if uart_ready_since else 993
                            channels[5] = 1800 if uart_ready_since and 0.35 < now-uart_ready_since < 0.55 else 200
                        else:
                            channels[2] = 1800
                            channels[5] = 1800 if 0.35 < now-recovery_since < 0.55 else 200
                    elif recovery_phase == 'neutral_ready':
                        channels[5] = 200
                    elif recovery_phase in ('rearm_ready','complete'):
                        channels[5] = 1800 if now-recovery_since > 0.2 and scenario != 'recovery_no_rearm' else 200
                if startup:
                    elapsed = now-control_started
                    channels[5] = 1800 if elapsed < 1.7 or elapsed >= 2.0 else 200
                    if elapsed < 1.2 or startup_timeout:
                        if scenario == 'motion_throttle_startup_silence':
                            continue
                        channels[2] = 0
                        flags = 12
                        if scenario == 'motion_throttle_startup_nonneutral':
                            channels[0], channels[2], flags = 1800, 200, 0
                bits = sum(value << (11*i) for i, value in enumerate(channels))
                os.write(master, b'\x0f'+bits.to_bytes(22, 'little')+bytes([flags,0]))
            if first_motion and now-first_motion > 0.3 and not injected and scenario in ('motion_sigterm', 'motion_throttle_sigterm'):
                fault_started = time.monotonic()
                process.send_signal(signal.SIGTERM)
                injected = True
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
        full_text = text
        text = '\n'.join(line for line in text.splitlines() if not line.startswith('event=trace_row'))
        assert process.returncode == (0 if success else 1), (
            text + '\nLAST CAN FRAMES:\n' + '\n'.join(
                f'{stamp:.6f} {ident:03x} {payload.hex()}' for stamp,ident,payload in trace[-60:]))
        if input_case:
            assert 'phase=input_observation ok=1' in text and not trace, text
        elif restoring:
            assert ('phase=baseline_restore ok=1' in text) == success
            assert values == (baseline if success else initial_values), (values, text)
            assert not words and not any(i == 0 and p == b'\x01\x01' for _,i,p in trace)
            writes = [p for _,i,p in trace if i == 0x601 and p[0] != 0x40]
            assert len(writes) == (18 if success else 0)
        elif scenario == 'motion_stop_timeout':
            assert 'control_hil_motion_stop_deadline' in text and 'restore_ok=0' in text, text
        else:
            assert 'restore_ok=1' in text, text
            assert all(v == baseline[k] for k,v in values.items() if not ((tolerant or standstill_noise) and k[0] == 0x606C)), (scenario, values, text)
            if scenario == 'recovery_jitter_reject':
                assert feedback_rejected and 'control_hil_trace_failure' in text
        if startup:
            assert 'event=input_status state=' in text, text
            assert all(p[0] not in (7, 15) and p[2:] == bytes(4)
                       for t,i,p in trace if i == 0x201 and t < control_started+2.0)
            if startup_timeout:
                assert first_motion is None and enabled_at is None
                assert 'control_hil_no_zero_enable' in text and 'event=motion_ready' not in text
                assert 2.4 <= time.monotonic()-control_started < 5.0
            else:
                assert first_motion and first_motion-control_started >= 2.0
                assert 'state=ready' in text and 'state=release_ch6' in text
                if scenario == 'motion_throttle_startup_nonneutral':
                    assert 'state=neutral_required' in text
                else:
                    assert 'state=waiting_link' in text
        if extended:
            assert first_motion and zero_requested and 'motion_window_ms=8000' in text
            assert 'event=motion_stop verified=1' in text
            if scenario == 'motion_throttle_long_failsafe':
                assert injected and 4.0 <= fault_started-first_motion < 5.0
                assert 0 <= zero_requested-fault_started < 0.1
                assert 'event=stop_cause code=5 ' in text and 'source_fault=4 ' in text
            else:
                assert 7.9 <= zero_requested-first_motion <= 8.05
                assert 'event=stop_cause code=2 ' in text
        if motion:
            if success:
                assert first_motion and zero_requested and 'event=motion_stop verified=1' in text, text
                assert zero_requested-first_motion <= motion_limit + 0.05
                assert any(ident == 0x181 and payload[4:] != bytes(4) for _, ident, payload in trace)
            if scenario in ('motion_both', 'motion_reverse', 'motion_throttle_steering', 'motion_throttle_reverse'):
                assert first_motion is None, 'forbidden command reached CAN'
            if scenario == 'motion_no_feedback':
                assert 'control_hil_no_motion_feedback' in text, text
            if scenario == 'motion_wrong_feedback':
                assert 'control_hil_motion_envelope' in text, text
            if scenario in ('motion_x1','motion_failsafe','motion_silence','motion_throttle_x1','motion_throttle_failsafe','motion_throttle_silence'):
                assert injected and zero_requested and 'event=motion_stop verified=1' in text
                assert 0 <= zero_requested-fault_started < (0.15 if scenario in ('motion_silence','motion_throttle_silence') else 0.1)
        if scenario in ('motion_sigterm', 'motion_throttle_sigterm'):
            assert injected and first_motion and zero_requested and first_motion < fault_started <= zero_requested
            assert 0 <= zero_requested-fault_started < 0.1
            assert 'event=stop_cause code=4 signal_exit=2 ' in text
            assert 'event=motion_stop verified=1' in text and 'phase=runtime_stop ok=1' in text
            assert all(p[2:] == bytes(4) for t,i,p in trace if i == 0x201 and t >= zero_requested)
        if not restoring and scenario not in ('missing_diagnostics', 'input_observation', 'input_export_timeout', 'recovery_option', 'motion_throttle_startup_timeout'):
            assert enabled_at and 0 in words and 6 in words and 7 in words and 15 in words, text
        if scenario in ('nonzero', 'x1_pulse', 'sigterm'):
            assert injected
        if recovery:
            if scenario == 'recovery_option':
                assert not any(i in (0,0x201) or (i == 0x601 and p[0] != 0x40) for _,i,p in trace)
            if success:
                assert injected and 'event=recovery phase=complete' in text, text
                assert 'event=recovery phase=neutral_ready' in text and 'event=recovery phase=rearm_ready' in text
            else:
                assert 'event=recovery phase=complete' not in text
        if scenario == 'nonzero':
            assert 'runtime_rpdo_send' in text or 'control_hil_zero_envelope' in text, text
        if scenario == 'input_export_timeout':
            assert stop_drain and 'event=trace_end' not in full_text
        else:
            check_trace(trace, full_text, scenario)
        if throttle_only:
            assert all(p[2:4] == bytes(2) for _,i,p in trace if i == 0x201)
        if success and not restoring and not startup and scenario not in ('input_observation', 'motion_throttle', 'motion_throttle_tolerance', 'motion_throttle_tolerance_reject', 'motion_stop_rebound', 'motion_throttle_long_deadline'):
            check_analyzer(trace, full_text, process.returncode, scenario)
        print(f'{scenario}: passed, zero_rpdo={len(words)}, restored=verified')
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        process.stdout.close()
        bus.close()
        os.close(master)
        os.close(slave)



def check_trace(wire, text, scenario):
    """Cross-check in-process evidence against the independent virtual peer and reject missing records."""
    from collections import Counter
    script = Path(__file__).resolve().parents[2]/'scripts/test/analyze_control_hil_trace.py'
    analyze = runpy.run_path(str(script))['analyze']
    result = analyze(text)
    if not scenario.startswith("restore_") and scenario not in ("missing_diagnostics", "recovery_option"): assert result["frames"] and result["cycles"]
    else: assert not result["frames"] and not result["cycles"]
    actual = Counter((i,p) for _,i,p in result['tx'])
    expected = Counter((i,p) for _,i,p in wire if i in (0,0x201,0x601))
    assert actual == expected, (actual-expected, expected-actual)
    observed = Counter((i,p) for _,i,p,_,_ in result['rx'])
    emitted = Counter((i,p) for _,i,p in wire if i not in (0,0x201,0x601))
    assert observed <= emitted, observed-emitted
    if scenario in ('happy','motion_left','motion_right','motion_deadline','motion_late_authorization'):
        assert observed == emitted, emitted-observed
    if scenario == 'motion_throttle_tolerance_reject':
        assert result['feedback_bad'] and 'phase=control ok=1' in text
        assert 'tenths_rpm=-21 tolerance_tenths_rpm=20 verdict=pending' in text
    if scenario == 'motion_throttle_tolerance':
        assert not result['feedback_bad'] and 'zero_feedback_tenths_rpm=20' in text
        runpy.run_path(str(script))['analyze_motion_feedback'](result,20,True)
        assert any(int.from_bytes(p[6:8], 'little', signed=True) == -20 for _,i,p,_,_ in result['rx'] if i == 0x181)
    if scenario in ('motion_throttle_long_failsafe', 'motion_throttle_long_deadline'):
        runpy.run_path(str(script))['analyze_motion_feedback'](result, 0, True, 8000)
        assert not result['feedback_bad']
        try:
            runpy.run_path(str(script))['analyze_motion_feedback'](result, 0, True)
        except AssertionError:
            pass
        else:
            raise AssertionError('Old 3s audit accepted extended motion')
    if scenario in ('motion_sigterm', 'motion_throttle_sigterm'):
        assert result['stops'][0]['cause'] == 4 and result['stops'][0]['lifecycle_exit'] == 2
        assert result['cycles'][-1][1][11:13] == [0,0] and not result['cycles'][-1][1][14]
        assert not result['feedback_bad']
        runpy.run_path(str(script))['analyze_motion_feedback'](result,0,scenario == 'motion_throttle_sigterm')
    if scenario == 'motion_throttle':
        moving = [d for _,d in result['cycles'] if d[12] > 0]
        assert moving and all(d[7] == d[8] > 0 and d[11] == 0 and d[14] for d in moving)
    if scenario == 'input_observation':
        assert not result['tx_attempts'] and not result['rx'] and not result['stops']
        assert all(not data[14] for _,data in result['cycles'])
        assert any(frame['mapped'][2] > 0 for frame in result['frames'])
    elif not scenario.startswith('restore_') and scenario not in ('missing_diagnostics', 'recovery_option'):
        assert len(result['stops']) == 1
    if scenario in ('motion_deadline','motion_left','motion_right','motion_late_authorization'):
        assert result['stops'][0]['cause'] == (2 if scenario == 'motion_deadline' else 1)
    if scenario == 'motion_stop_rebound':
        assert result['feedback_bad'] and 'phase=control ok=1' in text
        assert 'event=feedback_review ' in text and 'verdict=pending' in text
    if scenario in ('recovery_native_x1', 'recovery_x1','recovery_failsafe','recovery_silence','recovery_jitter','recovery_uart_partial','recovery_uart_clean','recovery_uart_resync','recovery_uart_bounce','recovery_uart_bounce_after_ready'):
        check_recovery_trace(result, text, 10 if scenario == "recovery_jitter" else 0)
    if scenario in ('recovery_uart_partial','recovery_uart_clean','recovery_uart_resync','recovery_uart_bounce','recovery_uart_bounce_after_ready'):
        parse = runpy.run_path(str(script))['fields']
        phases = {p['phase']:int(p['at_ns']) for p in (parse(line) for line in text.splitlines() if line.startswith('event=recovery '))}
        assert phases['release_fault']-phases['fault_observed'] >= 5000000000
        marks = [parse(line) for line in text.splitlines() if line.startswith('event=uart_reconnected ')]
        assert marks and all(int(p['at_ns'])-int(p['stable_since_ns']) >= 1000000000 for p in marks)
        if scenario == 'recovery_uart_bounce_after_ready':
            assert len(marks) == 2
        if scenario == 'recovery_uart_bounce':
            assert int(marks[0]['at_ns'])-phases['release_fault'] >= 1900000000
    if scenario == 'recovery_uart_resync':
        assert 'event=uart_resync ' in text
    if scenario == 'recovery_uart_partial':
        boundary = runpy.run_path(str(script))['fields'](next(line for line in text.splitlines() if line.startswith('event=uart_boundary ')))
        stamp = int(boundary['at_ns'])
        assert result['discontinuities'] == 1 and boundary['kind'] == 'partial_timeout'
        safe = next(t for t,i,p in result['tx'] if t >= stamp and i == 0x201 and p[0] in (0,2,6))
        assert 0 <= safe-stamp < 100000000
        assert all(d[14] == 0 for t,d in result['cycles'] if stamp <= t < stamp+1000000000)
    if scenario in ('recovery_uart_backlog','recovery_uart_service_gap','recovery_sbus_partial'):
        assert result['discontinuities'] and 'operation=control_hil_recovery' in text
    if scenario == 'recovery_uart_reconnect_timeout':
        assert 'operation=control_hil_recovery context=phase=2' in text
        phases = [runpy.run_path(str(script))['fields'](line) for line in text.splitlines() if line.startswith('event=recovery ')]
        released = int(next(p['at_ns'] for p in phases if p['phase'] == 'release_fault'))
        assert 45000000000 <= result['stops'][0]['ns']-released < 46000000000
    if scenario in ('recovery_uart_flags','recovery_uart_noise'):
        assert 'operation=control_hil_recovery' in text and 'phase=complete' not in text
    if scenario in ('happy','motion_left','input_observation'):
        for changed in (text.replace('overflow=0','overflow=1'),
                        '\n'.join(line for line in text.splitlines() if not line.startswith('event=trace_end'))):
            try:
                analyze(changed)
            except AssertionError:
                pass
            else:
                raise AssertionError('incomplete evidence accepted')


def check_recovery_trace(result, text, zero_tolerance=0):
    """Independently require zero targets, nonneutral inhibition, fresh rearm and real recovery state order."""
    script = Path(__file__).resolve().parents[2]/'scripts/test/analyze_control_hil_trace.py'
    analyze = runpy.run_path(str(script))['analyze_recovery']
    assert analyze(text, zero_tolerance)['fresh_authorization']
    for changed in (text.replace('phase=rearm_ready','phase=missing_rearm'),
                    text.replace('source_authorization=2','source_authorization=1'),
                    '\n'.join(line.replace('at_ns=', 'ignored_ns=') + ' at_ns=0' if 'phase=zero_reenabled ' in line else line for line in text.splitlines()),
                    text.replace(f'zero_feedback_tenths_rpm={zero_tolerance}', f'zero_feedback_tenths_rpm={0 if zero_tolerance else 10}')):
        try:
            analyze(changed, zero_tolerance)
        except (AssertionError,StopIteration):
            pass
        else:
            raise AssertionError('tampered recovery passed')


def check_analyzer(trace, application_log, returncode, scenario):
    """Replay virtual evidence through the physical-capture oracle and reject tampering."""
    script = Path(__file__).resolve().parents[2] / 'docs/verification/evidence/p10_3_control_zero_20260916/analyze-zero.py'
    motion = scenario.startswith('motion_')
    recovery = scenario.startswith('recovery_')
    if motion:
        script = script.parent.parent/'p10_3_control_motion_20260917/analyze-motion.py'
    if recovery:
        script = script.parent.parent/'p10_3_fault_recovery_20260917/zero_x1_a1/analyze-recovery.py'
    if scenario == 'recovery_jitter':
        script = script.parent.parent/'zero_x1_a3/analyze-recovery.py'
    prefix = 'recovery' if recovery else 'motion' if motion else 'zero'
    namespace = runpy.run_path(str(script))
    with tempfile.TemporaryDirectory(prefix='control-hil-oracle-') as directory:
        base = Path(directory)
        (base/f'{prefix}-target').mkdir()
        (base/f'{prefix}-jcan-once').mkdir()
        (base/f'{prefix}-target/application.log').write_text(application_log)
        (base/f'{prefix}-target/result.json').write_text(json.dumps({'passed':returncode == 0, 'application_exit':returncode}))
        (base/'authorization.json').write_text(json.dumps({'arguments':[
            '--single-right' if scenario == 'motion_right' else '--single-left']}))
        namespace['main'].__globals__['BASE'] = base
        for mutation in ('none', 'target', 'restore'):
            rows = list(trace)
            if mutation == 'target':
                i = next(i for i, (_, ident, _) in enumerate(rows) if ident == 0x201)
                stamp, ident, payload = rows[i]
                rows[i] = (stamp, ident, payload[:2]+bytes([6])+payload[3:])
            elif mutation == 'restore':
                i = next(i for i, (_, ident, payload) in enumerate(rows)
                         if ident == 0x601 and payload == bytes.fromhex('2300160208006060'))
                stamp, ident, payload = rows[i]
                rows[i] = (stamp, ident, payload[:4]+bytes.fromhex('2003ff60'))
            (base/f'{prefix}-target/candump.log').write_text(''.join(
                f'({stamp:.6f}) can0 {ident:03X} [{len(payload)}] {payload.hex(" ")}' + chr(10)
                for stamp, ident, payload in rows))
            (base/f'{prefix}-jcan-once/session.jsonl').write_text(''.join(
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
    for mode, options in (
        ('--right-throttle', ['--motion-window-ms', '-1']),
        ('--right-throttle', ['--motion-window-ms', '2999']),
        ('--right-throttle', ['--motion-window-ms', '8001']),
        ('--right-throttle', ['--motion-window-ms', '8000junk']),
        ('--right-throttle', ['--motion-window-ms', '8000', '--motion-window-ms', '3000']),
        ('--right-throttle', ['--zero-feedback-tenths-rpm', '0', '--zero-feedback-tenths-rpm', '15']),
        ('--zero-only', ['--motion-window-ms', '8000']),
        ('--observe-input', ['--motion-window-ms', '8000'])):
        checked = subprocess.run([os.environ['CONTROL_HIL_PROGRAM'], '--interface', 'vcan0',
                                  '--device', '/dev/nonexistent', '--duration-ms', '2000', mode, *options],
                                 capture_output=True, timeout=2)
        assert checked.returncode == 2, (mode, options, checked.returncode)
    for mode, tolerance in (('--zero-x1-recovery', '-1'), ('--zero-x1-recovery', '21'),
                            ('--zero-x1-recovery', '1junk'), ('--single-left', '21'), ('--right-throttle', '21'), ('--observe-input', '10')):
        checked = subprocess.run([os.environ['CONTROL_HIL_PROGRAM'], '--interface', 'vcan0', '--device', '/dev/nonexistent',
                                  '--duration-ms', '2000', mode, '--zero-feedback-tenths-rpm', tolerance],
                                 capture_output=True, timeout=2)
        assert checked.returncode == 2, (mode, tolerance, checked.returncode)
    for mode, duration, expected in (('--zero-uart-recovery', '120000', 1),
                                     ('--zero-uart-recovery', '120001', 2),
                                     ('--zero-sbus-recovery', '60001', 2)):
        checked = subprocess.run([os.environ['CONTROL_HIL_PROGRAM'], '--interface', 'vcan0',
                                  '--device', '/dev/nonexistent-f5-window-test', '--duration-ms', duration, mode],
                                 capture_output=True, timeout=2)
        assert checked.returncode == expected, (mode, duration, checked.returncode)
    cases = ('happy', 'nonzero', 'x1_pulse', 'sigterm', 'missing_diagnostics')
    if os.environ.get('CONTROL_HIL_MOTION_TEST') == '1':
        cases = ('motion_throttle_tolerance', 'motion_throttle_tolerance_reject', 'motion_throttle', 'motion_throttle_x1', 'motion_throttle_steering', 'motion_throttle_reverse', 'motion_throttle_failsafe', 'motion_throttle_silence', 'motion_left', 'motion_right', 'motion_deadline', 'motion_both', 'motion_reverse',
                 'motion_no_feedback', 'motion_wrong_feedback', 'motion_stop_timeout', 'motion_sigterm', 'motion_throttle_sigterm',
                 'motion_late_authorization', 'motion_stop_rebound', 'motion_x1', 'motion_failsafe', 'motion_silence',
                 'input_observation', 'input_export_timeout')
    if os.environ.get('CONTROL_HIL_RECOVERY_TEST') == '1':
        cases = ('recovery_native_x1', 'recovery_x1', 'recovery_failsafe', 'recovery_silence', 'recovery_missing', 'recovery_fault',
                 'recovery_nonzero', 'recovery_no_rearm', 'recovery_option', 'recovery_jitter', 'recovery_jitter_reject', 'restore_baseline', 'restore_foreign')
    if os.environ.get('CONTROL_HIL_UART_TEST') == '1':
        cases = ('recovery_uart_partial', 'recovery_uart_clean', 'recovery_uart_backlog',
                 'recovery_uart_flags', 'recovery_uart_noise', 'recovery_uart_service_gap', 'recovery_sbus_partial', 'recovery_uart_resync', 'recovery_uart_reconnect_timeout', 'recovery_uart_bounce', 'recovery_uart_bounce_after_ready')
    if os.environ.get('CONTROL_HIL_WINDOW_TEST') == '1':
        cases = ('motion_throttle_long_failsafe', 'motion_throttle_long_deadline')
    if os.environ.get('CONTROL_HIL_STARTUP_TEST') == '1':
        cases = ('motion_throttle_startup_failsafe', 'motion_throttle_startup_silence',
                 'motion_throttle_startup_nonneutral', 'motion_throttle_startup_timeout')
    for case in cases:
        trial(case)
