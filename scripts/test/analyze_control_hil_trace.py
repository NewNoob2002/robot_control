#!/usr/bin/env python3
"""Validate bounded HIL trace completeness and correlate raw SBUS with policy diagnostics."""
import argparse
import json
from pathlib import Path


def fields(line):
    """Parse the deliberately flat, whitespace-free diagnostic record format."""
    return dict(token.split('=', 1) for token in line.split() if '=' in token)


def normalize(raw, low, center, high):
    """Independent integer reference for the accepted asymmetric calibration."""
    delta = raw-center
    value = (abs(delta)*1000//(center-low if delta < 0 else high-center)) * (-1 if delta < 0 else 1)
    value = max(-1000, min(1000, value))
    return 0 if abs(value) <= 50 else value


def candidate(ch1, ch3):
    """Reference the fixed5rpm profile; never creates a hardware command."""
    steering = normalize(ch1, 200, 1000, 1800)
    throttle = normalize(ch3, 200, 993, 1800)
    def mix(value):
        """C++ integer truncation and the unchanged3rpm output deadband."""
        value = max(-1000, min(1000, value))
        rpm = (abs(value)*5//1000) * (-1 if value < 0 else 1)
        return 0 if abs(rpm) < 3 else rpm
    return steering, throttle, mix(throttle+steering), mix(throttle-steering)


def analyze(text):
    """Reject missing/overflowed trace and inconsistent raw/candidate linkage; report feedback failures."""
    lines = [fields(line) for line in text.splitlines() if line.startswith('event=trace_')]
    assert lines and lines[0]['event'] == 'trace_header' and lines[-1]['event'] == 'trace_end'
    header, end = lines[0], lines[-1]
    assert header['version'] == '1' and header['overflow'] == '0'
    rows = lines[1:-1]
    assert len(rows) == int(header['count']) == int(end['count']) <= int(header['capacity']) <= 65536
    batches = {}
    raw = {}
    frames = []
    cycles = []
    tx, rx, stops = [], [], []
    for index,row in enumerate(rows):
        assert row['event'] == 'trace_row' and int(row['ordinal']) == index
        stamp = int(row['ns'])
        data = [int(value) for value in row['fields'].split(',')]
        assert stamp >= 0 and len(data) == 16
        kind = int(row['kind'])
        if kind == 0:
            ident,session,discontinuity,raw_size,count = data[:5]
            assert ident not in batches and 0 <= count <= 256 and 0 <= raw_size <= 256
            batches[ident] = {'session':session, 'ns':stamp, 'count':count, 'events':0, 'discontinuity':discontinuity}
        elif kind in (1,2):
            ident,session,ordinal,ch1,ch3,ch6,ch7,flags,_ = data[:9]
            batch = batches[ident]
            assert session == batch['session'] and stamp == batch['ns'] and ordinal == batch['events']
            batch['events'] += 1
            if kind == 1:
                assert all(0 <= value <= 2047 for value in (ch1,ch3,ch6,ch7))
                raw[session,stamp] = (ch1,ch3,ch6,ch7,flags)
                frames.append({'ns':stamp,'session':session,'ch1':ch1,'ch3':ch3,'ch6':ch6,
                               'flags':flags,'mapped':candidate(ch1,ch3)})
        elif kind == 3:
            cycles.append((stamp,data))
            # Healthy snapshots must use the raw frame identified by the source receive timestamp/session.
            if data[15] in (2,3):
                sample = raw[data[3],data[4]]
                assert tuple(data[5:9]) == candidate(sample[0],sample[1]), (data,sample)
        elif kind in (4,5):
            ident,dlc = data[:2]
            assert 0 <= dlc <= 8
            payload = bytes(data[2:2+dlc])
            (rx if kind == 4 else tx).append((stamp,ident,payload,data[10],data[11]))
        elif kind == 6:
            assert 0 <= data[0] <= 7
            stops.append({'ns':stamp,'cause':data[0],'lifecycle_exit':data[1],'source_sequence':data[2],'source_ns':data[3]})
        else:
            raise AssertionError(f'Unknown trace kind {kind}')
    assert all(v['count'] == v['events'] for v in batches.values())
    accepted = [(t,i,p) for t,i,p,result,error in tx if result == 16 and error == 0]
    return {'count':len(rows),'feedback_bad':header['feedback_bad']=='1','frames':frames,'cycles':cycles,
            'tx':accepted,'tx_attempts':tx,'rx':rx,'stops':stops,
            'discontinuities':sum(v['discontinuity']!=0 for v in batches.values())}


def analyze_recovery(text, zero_tolerance=0):
    """Verify the zero-only recovery protocol independently of the application's completion flag."""
    assert 0 <= zero_tolerance <= 20
    header = next(fields(line) for line in text.splitlines() if line.startswith('event=trace_header'))
    assert int(header.get('zero_feedback_tenths_rpm', '0')) == zero_tolerance
    result = analyze(text)
    phases = [fields(line) for line in text.splitlines() if line.startswith('event=recovery ')]
    names = ('fault_ready','fault_observed','release_fault','neutral_ready','rearm_ready','zero_reenabled','complete')
    assert [p['phase'] for p in phases] == list(names)
    marks = {p['phase']:int(p['at_ns']) for p in phases}
    assert all(int(a['at_ns']) < int(b['at_ns']) for a,b in zip(phases,phases[1:]))
    assert len({p['kind'] for p in phases}) == 1 and phases[0]['kind'] in ('x1','sbus')
    assert not result['feedback_bad'] and result['stops'][0]['cause'] == 7
    assert marks['release_fault']-marks['fault_observed'] >= 1000000000
    assert marks['complete']-marks['zero_reenabled'] >= 1000000000
    assert int(phases[-1]['source_authorization']) > int(phases[0]['source_authorization']) > 0
    assert int(phases[-1]['system_authorization']) > int(phases[0]['system_authorization']) > 0
    tx = [(t,p) for t,i,p in result['tx'] if i == 0x201]
    rx = [(t,p) for t,i,p,_,_ in result['rx'] if i == 0x181]
    assert tx and rx and all(len(p) == 6 and p[2:] == bytes(4) for _,p in tx)
    assert all(abs(int.from_bytes(p[offset:offset+2], 'little', signed=True)) <= zero_tolerance
               for _,p in rx for offset in (4,6))
    if zero_tolerance:
        holds = [fields(line) for line in text.splitlines() if line.startswith('event=standstill_verified ')]
        assert len(holds) == 2 and all(int(h['tolerance_tenths_rpm']) == zero_tolerance
                                     and int(h['elapsed_us']) >= 150000 for h in holds)
    assert all(p[0] in (0,2,6) for t,p in tx if marks['fault_observed'] <= t < marks['rearm_ready'])
    challenge = [(t,d) for t,d in result['cycles'] if marks['release_fault'] <= t <= marks['neutral_ready'] and any(d[7:9])]
    assert challenge and challenge[-1][0]-challenge[0][0] >= 990000000
    assert all(d[11:13] == [0,0] for _,d in challenge)
    assert any(marks['release_fault'] <= r['ns'] <= marks['neutral_ready'] and r['ch6'] >= 1500
               and r['flags'] == 0 and any(r['mapped'][2:]) for r in result['frames'])
    if phases[0]['kind'] == 'x1':
        assert any(marks['fault_ready'] <= t < marks['release_fault'] and int.from_bytes(p[:4],'little') & 0x8000 for t,p in rx)
    else:
        assert any(marks['fault_ready'] <= r['ns'] < marks['release_fault'] and r['flags'] & 12 for r in result['frames']) or any(
            marks['fault_ready'] <= t < marks['release_fault'] and t-d[4] >= 100000000 and not d[14] for t,d in result['cycles'])

    def status_at(after, value, mask=0x6f):
        """Require a newer dual-axis feedback state on the application's independent receive clock."""
        return next(t for t,p in rx if after < t <= first_enabled and all(int.from_bytes(p[i:i+2],'little') & mask == value for i in (0,2)))

    def command_at(after, value):
        """Require a subsequent actual successful RPDO syscall, not just a policy candidate."""
        return next(t for t,p in tx if after < t <= first_enabled and p[0] == value)

    first_enabled = next(t for t,p in rx if t > marks['rearm_ready']
                         and all(int.from_bytes(p[i:i+2], 'little') & 0x6f == 0x27 for i in (0,2)))
    assert 0 <= marks['zero_reenabled'] - first_enabled <= 100000000, 'late/misattributed recovery announcement'
    rearmed = [(t,d) for t,d in result['cycles'] if marks['rearm_ready'] <= t <= marks['complete']
               and d[2] > int(phases[0]['source_authorization'])]
    assert rearmed and len({d[2] for _,d in rearmed}) == 1 and all(d[14] for _,d in rearmed)
    assert rearmed[0][1][2] == int(phases[-1]['source_authorization'])
    assert rearmed[0][0] < first_enabled
    quick_stops = [t for t,p in rx if marks['fault_observed'] < t < first_enabled
                   and all(int.from_bytes(p[i:i+2], 'little') & 0x6f == 7 for i in (0,2))]
    if quick_stops:
        disabled_command = command_at(max(quick_stops[-1], marks['rearm_ready']), 0)
        disabled = status_at(disabled_command, 0x40, 0x4f)
    else:
        assert phases[0]['kind'] == 'x1'
        disabled = status_at(marks['fault_observed'], 0x40, 0x4f)
    shutdown = command_at(max(disabled, rearmed[0][0]), 6)
    ready = status_at(shutdown, 0x21)
    switched = status_at(command_at(ready, 7), 0x23)
    enabled = status_at(command_at(switched, 15), 0x27)
    assert enabled == first_enabled
    return {'kind':phases[0]['kind'],'zero_feedback_tenths_rpm':zero_tolerance,'phases':marks,'targets_zero':True,'nonneutral_inhibited':True,
            'fresh_authorization':True,'recovery_path':'quick_stop' if quick_stops else 'native_x1_disabled',
            'first_rearm_state_order':True}



def analyze_motion_feedback(result, zero_tolerance=15, right=True, motion_window_ms=3000):
    """Audit signed feedback by actual target phase; tolerance never permits reverse motion commands."""
    assert 0 <= zero_tolerance <= 20
    assert 3000 <= motion_window_ms <= 8000
    tx = [(t,p) for t,i,p in result['tx'] if i == 0x201]
    rx = [(t,p) for t,i,p,_,_ in result['rx'] if i == 0x181]
    selected_offset, other_offset = (4,2) if right else (2,4)
    def signed(payload, offset):
        """Decode exact signed16-bit wire values without clamping."""
        return int.from_bytes(payload[offset:offset+2], 'little', signed=True)
    assert tx and rx
    assert all(signed(p,other_offset) == 0 and 0 <= signed(p,selected_offset) <= 5 for _,p in tx)
    first = next(t for t,p in tx if signed(p,selected_offset))
    zero = next(t for t,p in tx if t > first and not signed(p,selected_offset))
    assert 0 < zero-first < motion_window_ms * 1000000
    assert all(not signed(p,selected_offset) for t,p in tx if t >= zero)
    assert all(p[0] == 15 for _,p in tx if signed(p,selected_offset))
    speeds = [(t,signed(p,selected_offset+2),signed(p,other_offset+2)) for t,p in rx]
    assert all(abs(other) <= zero_tolerance for _,_,other in speeds)
    assert all(abs(v) <= zero_tolerance if t < first else (-zero_tolerance <= v <= 75)
               for t,v,_ in speeds)
    assert sum(v > zero_tolerance for t,v,_ in speeds if first <= t < zero) >= 2
    since = reached = None
    for t,v,other in speeds:
        if t <= zero:
            continue
        if abs(v) > zero_tolerance or abs(other) > zero_tolerance:
            since = reached = None
        else:
            since = t if since is None else since
            if reached is not None and t > reached:
                assert t-zero <= 1000000000
                return {'zero_feedback_tenths_rpm':zero_tolerance, 'first_nonzero_ns':first,
                        'first_zero_ns':zero, 'stable_since_ns':since, 'stable_verified_ns':t,
                        'stop_to_verified_ms':(t-zero)/1e6, 'raw_feedback_preserved':True}
            if t-since >= 150000000:
                reached = t
    raise AssertionError('No fresh bounded standstill hold')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log',type=Path)
    parser.add_argument('--recovery',action='store_true')
    parser.add_argument('--zero-feedback-tenths-rpm',type=int,default=0)
    args=parser.parse_args()
    result=analyze(args.log.read_text())
    if args.recovery:
        print(json.dumps(analyze_recovery(args.log.read_text(), args.zero_feedback_tenths_rpm),indent=2))
    print(json.dumps({'trace_records':result['count'],'frames':len(result['frames']),
                      'cycles':len(result['cycles']),'accepted_tx':len(result['tx']),
                      'feedback_bad':result['feedback_bad'],'stops':result['stops'],
                      'discontinuities':result['discontinuities']},indent=2))
