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
    if 'binding_version' in header:
        assert header['binding_version'] == '1' and 'right_sign' in header
    right_sign = int(header.get('right_sign','1'))
    assert right_sign in (-1,1)
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
    return {'right_sign':right_sign,'count':len(rows),'feedback_bad':header['feedback_bad']=='1','frames':frames,'cycles':cycles,
            'tx':accepted,'tx_attempts':tx,'rx':rx,'stops':stops,
            'discontinuities':sum(v['discontinuity']!=0 for v in batches.values()),
            'motion_mode':int(header.get('motion_mode', '-1')),
            'zero_tolerance':int(header.get('zero_feedback_tenths_rpm','0'))}


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
    right_sign = result.get('right_sign',1)
    assert right_sign in (-1,1)
    def signed(payload, offset):
        """Normalize the right half without modifying raw wire records."""
        physical_right = offset == (4 if len(payload) == 6 else 6)
        return int.from_bytes(payload[offset:offset+2], 'little', signed=True) * (right_sign if physical_right else 1)
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


def analyze_g3_feedback(result, profile, zero_tolerance=20):
    """Audit signed G3 targets, two-axis feedback and bounded stop independently of C++ predicates."""
    profiles = {
        'single-left-negative': (4, -1, 0), 'single-right-negative': (5, 0, -1),
        'dual-positive': (6, 1, 1), 'dual-negative': (7, -1, -1),
        'turn-left': (8, -1, 1), 'turn-right': (9, 1, -1),
        'differential-left': (10, 1, 1), 'differential-right': (11, 1, 1),
    }
    mode, *signs = profiles[profile]
    right_sign = result.get('right_sign',1)
    assert right_sign in (-1,1)
    assert result['motion_mode'] == mode and 0 <= zero_tolerance <= 20
    assert result['zero_tolerance'] == zero_tolerance
    assert not result['feedback_bad'] and len(result['stops']) == 1
    def pair(payload, offset):
        """Decode both signed little-endian wheel values without saturation."""
        left = int.from_bytes(payload[offset:offset+2], 'little', signed=True)
        right = int.from_bytes(payload[offset+2:offset+4], 'little', signed=True)
        return left, right * right_sign
    tx = [(t,p) for t,i,p in result['tx'] if i == 0x201]
    assert all(len(p) == 8 for _,i,p,_,_ in result['rx'] if i == 0x181)
    rx = [(t,pair(p,4)) for t,i,p,_,_ in result['rx'] if i == 0x181]
    moving = [(t,p) for t,p in tx if any(pair(p,2))]
    assert moving and rx
    for _,p in tx:
        assert len(p) == 6
        left,right = pair(p,2)
        if left == right == 0: continue
        assert p[:2] == bytes([15,0])
        assert all(1 <= value*sign <= 5 if sign else value == 0 for value,sign in zip((left,right),signs))
        if profile.startswith('dual-'): assert left == right
        if profile.startswith('turn-'): assert left == -right
        if profile == 'differential-left': assert left <= right
        if profile == 'differential-right': assert left >= right
    first = moving[0][0]
    zero = next(t for t,p in tx if t > first and not any(pair(p,2)))
    assert 0 < zero-first < 3000000000
    assert all(not any(pair(p,2)) for t,p in tx if t >= zero), 'restart after zero'
    for t,values in rx:
        for value,sign in zip(values,signs):
            assert abs(value) <= zero_tolerance if t < first or not sign else -zero_tolerance <= value*sign <= 75
    counts = [sum(value[axis]*sign > zero_tolerance for t,value in rx if first <= t < zero) if sign else 0
              for axis,sign in enumerate(signs)]
    assert all(count >= 2 for count,sign in zip(counts,signs) if sign), 'missing axis motion feedback'
    assert sum(all(value[axis]*sign > zero_tolerance for axis,sign in enumerate(signs) if sign)
               for t,value in rx if first <= t < zero) >= 2, 'missing simultaneous motion feedback'
    differential_samples = 0
    if profile.startswith('differential-'):
        # Correlate each fresh feedback with the latest strictly earlier wire command.
        command = iter(tx)
        following = next(command, None)
        active = (0,0)
        last_sample = None
        for t,values in rx:
            while following is not None and following[0] < t:
                active = pair(following[1],2)
                following = next(command,None)
            if (first < t < zero and t != last_sample and min(active) > 0 and active[0] != active[1]
                    and min(values) > zero_tolerance
                    and (values[0] < values[1] if profile == 'differential-left' else values[0] > values[1])):
                differential_samples += 1
            last_sample = t
        assert differential_samples >= 2, 'missing differential command/feedback evidence'
    since = reached = None
    for t,values in rx:
        if t <= zero: continue
        if any(abs(v) > zero_tolerance for v in values):
            since = reached = None
            continue
        since = t if since is None else since
        if reached is not None and t > reached:
            assert t-zero <= 1000000000
            report = dict(profile=profile, first_nonzero_ns=first, first_zero_ns=zero,
                          axis_motion_samples=counts, stable_since_ns=since, stable_verified_ns=t)
            if profile.startswith('differential-'):
                report['differential_feedback_samples'] = differential_samples
            return report
        if t-since >= 150000000: reached = t
    raise AssertionError('No fresh dual-axis 150ms standstill hold within 1s')


def analyze_g3_captures(result, wire, independent, profile, zero_tolerance=20):
    """Require exact dual-capture order, trace TX identity, bounded motion and the reviewed volatile lifecycle."""
    assert wire and [(i,p) for _,i,p in wire] == independent, 'global capture order/payload mismatch'
    assert [(i,p) for _,i,p in result['tx']] == [(i,p) for _,i,p in wire if i in (0,0x201,0x601)]
    # Use only wire timestamps for the independent stop audit, never subtract clock domains.
    captured = dict(result, tx=[(round(t*1e9),i,p) for t,i,p in wire if i in (0,0x201,0x601)],
                    rx=[(round(t*1e9),i,p,16,0) for t,i,p in wire if i not in (0,0x201,0x601)])
    timing = analyze_g3_feedback(captured,profile,zero_tolerance)
    first,last,writes,nmts = {},{},{},[]
    downloads = []
    pending = None
    for stamp,ident,payload in wire:
        assert ident in (0,0x181,0x201,0x281,0x381,0x481,0x581,0x601,0x701)
        if ident == 0: nmts.append(payload.hex())
        elif ident == 0x201:
            assert payload[0] in (0,2,6,7,15) and payload[1] == 0
        elif ident == 0x281: assert payload == bytes([3,0,0,0,0])
        elif ident == 0x701: assert payload in (bytes([5]),bytes([127]))
        elif ident in (0x381,0x481): assert not payload
        elif ident == 0x181:
            assert all(int.from_bytes(payload[i:i+2],'little') & 0x8008 == 0 for i in (0,2))
        elif ident == 0x601:
            assert len(payload) == 8 and pending is None
            pending = payload
            key = (int.from_bytes(payload[1:3],'little'),payload[3])
            if payload[0] != 0x40:
                assert payload[0] in (0x2f,0x2b,0x23)
                downloads.append((*key,int.from_bytes(payload[4:],'little')))
                assert key == (0x6040,0) or key in first, 'write without baseline readback'
                writes[key] = stamp
        elif ident == 0x581:
            assert pending is not None and len(payload) == 8 and pending[1:4] == payload[1:4]
            key = (int.from_bytes(payload[1:3],'little'),payload[3])
            if pending[0] == 0x40:
                assert payload[0] in (0x43,0x4b,0x4f)
                width = {0x43:4,0x4b:2,0x4f:1}[payload[0]]
                value = int.from_bytes(payload[4:4+width],'little')
                first.setdefault(key,value);last[key] = (stamp,value)
                if key[0] == 0x606c:
                    offsets = (4,6) if key[1] == 3 else (4,)
                    size = 2 if key[1] == 3 else 4
                    assert all(abs(int.from_bytes(payload[i:i+size],'little',signed=True)) <= zero_tolerance for i in offsets)
            else: assert payload[0] == 0x60
            pending = None
    rpdo = [(0x1400,1,0x80000201),(0x1600,0,0),(0x1600,1,0x60400010),
            (0x1600,2,0x60ff0320),(0x1600,0,2),(0x1400,1,0x201)]
    tpdo = [(0x1801,1,0x80000281),(0x1a01,0,0),(0x1a01,1,0x60610008),
            (0x1a01,2,0x603f0020),(0x1a01,0,2),(0x1801,5,100),(0x1801,1,0x281)]
    expected = [(0x1017,0,500),(0x2000,0,1000)] + rpdo + tpdo + [(0x60ff,1,0),(0x60ff,2,0),(0x6040,0,6)]
    expected += [(0x60ff,1,0),(0x60ff,2,0),(0x6040,0,0)]
    expected += [(i,s,0x60600008 if (i,s)==(0x1600,2) else v) for i,s,v in rpdo]
    expected += [(i,s,0 if i==0x1a01 or (i,s)==(0x1801,5) else v) for i,s,v in tpdo]
    expected += [(0x2000,0,0),(0x1017,0,0)]
    assert pending is None and downloads == expected and nmts == ['8001','0101','8001']
    assert all(last[key][0] > stamp and last[key][1] == first[key] for key,stamp in writes.items() if key != (0x6040,0))
    assert last[0x6040,0][1] == 0 and last[0x6040,0][0] > writes[0x6040,0]
    return dict(matching_frames=len(wire), volatile_writes=len(downloads), baseline_restored=True,
                wire_motion=timing, scope='Normal bounded G3 trial only; separate physical direction/operator acceptance required')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log',type=Path)
    parser.add_argument('--recovery',action='store_true')
    parser.add_argument('--right-sign',type=int,choices=(-1,1),help='Require the recorded physical binding; omitted headers mean legacy+1')
    parser.add_argument('--candump',type=Path)
    parser.add_argument('--jcan',type=Path)
    parser.add_argument('--motion-profile', choices=('single-left-negative','single-right-negative','dual-positive','dual-negative','turn-left','turn-right','differential-left','differential-right'))
    parser.add_argument('--zero-feedback-tenths-rpm',type=int,default=0)
    args=parser.parse_args()
    if bool(args.candump) != bool(args.jcan) or (args.candump and not args.motion_profile):
        parser.error('--candump and --jcan require each other and --motion-profile')
    result=analyze(args.log.read_text())
    if args.right_sign is not None:
        assert result['right_sign'] == args.right_sign, 'recorded binding differs from trial'
    if args.candump:
        import sys
        sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'hil'))
        from phase6_zero_motion_soak import parse_capture
        summary = fields(next(line for line in args.log.read_text().splitlines() if line.startswith('event=summary ')))
        assert summary['primary_ok'] == summary['restore_ok'] == '1'
        independent = []
        for line in args.jcan.read_text().splitlines():
            frame = json.loads(line)
            if frame.get('event') == 'frame':
                assert not any(frame[k] for k in ('brs','extended','fd','remote'))
                independent.append((frame['can_id'],bytes.fromhex(frame['data_hex'])))
        print(json.dumps(analyze_g3_captures(result,parse_capture(args.candump.read_text()),independent,
                                           args.motion_profile,args.zero_feedback_tenths_rpm),indent=2))
    if args.motion_profile:
        print(json.dumps(analyze_g3_feedback(result, args.motion_profile, args.zero_feedback_tenths_rpm),indent=2))
    if args.recovery:
        print(json.dumps(analyze_recovery(args.log.read_text(), args.zero_feedback_tenths_rpm),indent=2))
    print(json.dumps({'trace_records':result['count'],'frames':len(result['frames']),
                      'cycles':len(result['cycles']),'accepted_tx':len(result['tx']),
                      'feedback_bad':result['feedback_bad'],'stops':result['stops'],
                      'discontinuities':result['discontinuities']},indent=2))
