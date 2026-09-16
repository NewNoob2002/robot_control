"""Independently validate raw decoding, mapped snapshots and manual P9.3 HIL gates."""
import gzip
import hashlib
import json
from pathlib import Path
import re
import sys


def normalize(raw, axis, deadband):
    """Use exact truncation toward zero, asymmetric calibration, inversion and deadband."""
    delta = raw - axis['center']
    span = axis['center'] - axis['minimum'] if delta < 0 else axis['maximum'] - axis['center']
    value = (abs(delta) * 1000 // span) * (-1 if delta < 0 else 1)
    value = max(-1000, min(1000, value))
    if axis['reversed']:
        value = -value
    return 0 if abs(value) <= deadband else value


def candidate(channels, profile):
    """Return independent axis and candidate-RPM oracles, never actual drive commands."""
    steering = normalize(channels[0], profile['steering'], profile['input_deadband_per_mille'])
    throttle = normalize(channels[2], profile['throttle'], profile['input_deadband_per_mille'])
    gear = profile['gear_rpm'][0 if channels[6] <= profile['gear_low'] else 2 if channels[6] >= profile['gear_high'] else 1]
    values = []
    for value in (throttle + steering, throttle - steering):
        value = max(-1000, min(1000, value))
        rpm = (abs(value) * gear // 1000) * (-1 if value < 0 else 1)
        rpm = max(-profile['maximum_rpm'], min(profile['maximum_rpm'], rpm))
        values.append(0 if abs(rpm) < profile['output_deadband_rpm'] else rpm)
    return steering, throttle, *values


def analyze(root):
    """Preserve original human timings; reject failed gates without retry or relabeling."""
    root = Path(root)
    content = gzip.decompress((root / 'capture.log.gz').read_bytes())
    profile = json.loads((root / 'profile.json').read_text())
    result = json.loads((root / 'result.json').read_text())
    timeline = [json.loads(line) for line in (root / 'timeline.jsonl').read_text().splitlines()]
    relay = [json.loads(line) for line in (root / 'relay.jsonl').read_text().splitlines()]
    started = result['start_mono_ns']
    reads, frames, rejected, sources, errors = [], [], [], [], []
    wire = bytearray()
    marker = False
    last_frame = None
    last_sequence = 0
    previous_authorization = 0
    for line in content.decode().splitlines():
        if line.startswith('event=read '):
            fields = dict(token.split('=', 1) for token in line.split())
            received = int(fields['received_ns'])
            assert int(fields['discontinuity']) == 0, line
            assert not reads or received > reads[-1]
            reads.append(received)
            for byte in bytes.fromhex(fields['kernel_raw']):
                if marker:
                    assert byte == 255, 'Kernel line error'
                    wire.append(255)
                    marker = False
                elif byte == 255:
                    marker = True
                else:
                    wire.append(byte)
        elif line.startswith('event=frame '):
            fields = dict(token.split('=', 1) for token in line.split())
            channels = [int(v) for v in fields['channels'].strip(',').split(',')]
            assert len(channels) == 16 and int(fields['sequence']) == len(frames) + 1
            last_frame = dict(time=reads[-1], second=(reads[-1] - started)/1e9,
                              channels=channels, flags=int(fields['flags']), session=int(fields['session']))
            frames.append(last_frame)
        elif line.startswith('event=rejected '):
            rejected.append(reads[-1])
        elif line.startswith('event=source '):
            fields = dict(token.split('=', 1) for token in line.split())
            state = {key: value if key in ('event', 'fault', 'last_fault') else int(value)
                     for key, value in fields.items()}
            assert state['sequence'] > last_sequence and state['authorization'] >= previous_authorization
            last_sequence = state['sequence']
            state['second'] = (state['captured_ns'] - started)/1e9
            if last_frame is not None:
                assert state['captured_ns'] == last_frame['time'] and state['session'] == last_frame['session']
                expected = candidate(last_frame['channels'], profile)
                assert tuple(state[key] for key in ('steering', 'throttle', 'candidate_left', 'candidate_right')) == expected
                assert state['lost'] == bool(last_frame['flags'] & 4)
                assert state['failsafe'] == bool(last_frame['flags'] & 8)
                state['button'] = last_frame['channels'][5]
            else:
                assert not state['valid']
                state['button'] = None
            if state['valid']:
                assert state['coherent'] and state['enabled'] and state['authorization'] and not state['stop']
                assert not state['lost'] and not state['failsafe']
                if state['authorization'] != previous_authorization:
                    assert state['left_rpm'] == state['right_rpm'] == 0
                else:
                    assert state['left_rpm'] == state['candidate_left'] and state['right_rpm'] == state['candidate_right']
            else:
                assert not state['enabled'] and state['stop'] and state['left_rpm'] == state['right_rpm'] == 0
            previous_authorization = state['authorization']
            sources.append(state)
        elif line.startswith('event=error '):
            errors.append(line)
        elif line.startswith('event=start '):
            assert 'baud=100000 data_bits=8 parity=even stop_bits=2 parmrk=1' in line
            assert 'steering_axis=200,1000,1800,0 throttle_axis=200,993,1800,0' in line
        else:
            assert line == f'event=summary reason=signal signal=15 frames={len(frames)}', line
    assert not marker and not errors
    offset, decoded, rejected_offsets = 0, [], []
    while offset < len(wire):
        header = wire.find(bytes([15]), offset)
        if header < 0 or header + 25 > len(wire):
            break
        if wire[header + 24] != 0:
            rejected_offsets.append(header)
            offset = header + 1
        else:
            decoded.append(wire[header:header+25])
            offset = header + 25
    assert len(decoded) == len(frames) and len(rejected_offsets) == len(rejected)
    for data, frame in zip(decoded, frames):
        packed = int.from_bytes(data[1:23], 'little')
        assert frame['channels'] == [(packed >> (11*i)) & 2047 for i in range(16)]
        assert frame['flags'] == data[23]
    valid_generations = sorted(set(s['authorization'] for s in sources if s['valid']))
    gates = {}
    initial = [s for s in sources if 0 <= s['second'] < 2]
    gates['startup_held_disabled'] = bool(initial) and all(s['button'] >= 1500 and not s['valid'] for s in initial)
    gates['three_fresh_authorizations'] = valid_generations == [1, 2, 3]
    gates['normal_disable'] = any(15 <= s['second'] < 18 and s['authorization'] == 1 and not s['valid'] and s['fault'] == 'none' for s in sources)
    gates['nonneutral_press_inhibited'] = any(18 <= s['second'] < 24 and s['button'] >= 1500 and (s['steering'] or s['throttle']) and not s['valid'] and s['authorization'] == 1 for s in sources)
    gates['neutral_no_queued_rearm'] = any(21 <= s['second'] < 24 and s['steering'] == s['throttle'] == 0 and s['button'] <= 500 and not s['valid'] and s['authorization'] == 1 for s in sources)
    flagged = [s for s in sources if s['lost'] or s['failsafe']]
    gates['lost_and_failsafe_inhibit'] = bool(flagged) and any(s['lost'] for s in flagged) and any(s['failsafe'] for s in flagged) and all(not s['valid'] for s in flagged)
    gates['recovered_but_disabled'] = any(33 <= s['second'] < 39 and s['health'] == 2 and s['fault'] == 'none' and not s['valid'] and s['authorization'] == 2 for s in sources)
    gates['nonzero_before_sigterm'] = any(s['second'] >= 43 and s['valid'] and (s['left_rpm'] or s['right_rpm']) and s['authorization'] == 3 for s in sources)
    gates['shutdown_zero'] = sources[-1]['fault'] == 'shutdown' and not sources[-1]['valid'] and sources[-1]['left_rpm'] == sources[-1]['right_rpm'] == 0
    gates['bounded_sigterm'] = result['exit_code'] == 143 and 45000 <= result['run_to_signal_ms'] < 45500 and result['signal_to_reaped_ms'] < 1000
    gates['desktop_notifications'] = all(row['notification_exit_code'] == 0 for row in relay)
    transitions, previous = [], None
    for state in sources:
        identity = tuple(state[k] for k in ('authorization', 'valid', 'health', 'fault', 'lost', 'failsafe'))
        if identity != previous:
            transitions.append({k:state[k] for k in ('second','authorization','valid','health','fault','lost','failsafe','left_rpm','right_rpm')})
            previous = identity
    return dict(capture_sha256=hashlib.sha256(content).hexdigest(), frames=len(frames), reads=len(reads),
                snapshots=len(sources), rejected=len(rejected), independent_decode_matches=True,
                all_snapshot_mapping_matches=True, valid_authorizations=valid_generations, gates=gates,
                passed=all(gates.values()), transitions=transitions, target_exit=result,
                first_phase_utc=next(e['utc'] for e in timeline if e['kind']=='phase'))


if __name__ == '__main__':
    report = analyze(sys.argv[1])
    print(json.dumps(report, indent=2, ensure_ascii=False))
    raise SystemExit(0 if report['passed'] else 1)
