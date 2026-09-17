"""Preserve right A1 failure while independently checking its commands, stopping trace and restoration."""
from pathlib import Path
import importlib.util
import json
import struct

BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[4]


def main():
    """Audit the complete one-shot evidence; never reclassify negative feedback as a pass."""
    records = []
    for line in (BASE/'motion-target/candump.log').read_text().splitlines():
        fields = line.split()
        assert fields[1] == 'can0'
        payload = bytes.fromhex(' '.join(fields[4:]))
        assert len(payload) == int(fields[3][1:-1])
        records.append((float(fields[0][1:-1]), int(fields[2],16), payload))
    independent = []
    for line in (BASE/'motion-jcan-once/session.jsonl').read_text().splitlines():
        row = json.loads(line)
        assert row.get('ok') is not False and not row.get('error') and not row.get('warnings')
        if row.get('event') == 'frame':
            assert not any(row[key] for key in ('brs','extended','fd','remote'))
            independent.append((row['can_id'],bytes.fromhex(row['data_hex'])))
    assert independent == [(i,p) for _,i,p in records]
    assert not (BASE/'motion-target/candump.stderr').read_text()
    for _,ident,p in records:
        assert ident in (0,0x181,0x201,0x281,0x381,0x481,0x581,0x601,0x701)
        if ident == 0x181:
            assert len(p) == 8
            for offset in (0,2):
                word = int.from_bytes(p[offset:offset+2],'little')
                assert word & 0x6f in (0,0x07,0x21,0x23,0x27,0x40,0x60) and word & 0x8000 == 0
        elif ident == 0x201:
            assert len(p) == 6 and p[1] == 0 and p[0] in (0,2,6,7,15)
        elif ident == 0x281: assert p == bytes([3,0,0,0,0])
        elif ident in (0x381,0x481): assert p == b''
        elif ident == 0x701: assert p in (bytes([5]),bytes([127]))
    targets = [(t,p[0],*reversed(struct.unpack('<hh',p[2:]))) for t,i,p in records if i == 0x201]
    assert all(0 <= selected <= 5 and other == 0 for _,_,selected,other in targets)
    moving = [row for row in targets if row[2]]
    first = moving[0][0]
    stop = next(t for t,_,selected,_ in targets if t > first and selected == 0)
    assert all(word == 15 for _,word,_,_ in moving)
    assert 0 < stop-first < 3 and all(selected == 0 for t,_,selected,_ in targets if t >= stop)
    velocities = [(t,*reversed(struct.unpack('<hh',p[4:]))) for t,i,p in records if i == 0x181]
    assert all(other == 0 for _,_,other in velocities)
    assert all(selected == 0 for t,selected,_ in velocities if t < first)
    assert all(0 <= selected <= 75 for t,selected,_ in velocities if first <= t < stop)
    moving_speeds = [selected for t,selected,_ in velocities if first <= t < stop]
    assert sum(speed > 0 for speed in moving_speeds) >= 2
    negative = [{'after_stop_ms':(t-stop)*1000,'right_tenths_rpm':selected}
                for t,selected,_ in velocities if selected < 0]
    assert negative and all(row['after_stop_ms'] > 0 for row in negative)
    last_moving = max(t for t,selected,_ in velocities if t >= stop and selected != 0)
    zeros = [t for t,selected,_ in velocities if t > last_moving and selected == 0]
    assert zeros[-1]-zeros[0] >= .15 and zeros[-1]-stop <= 1
    writes, nmts, readbacks = [], [], {}
    pending = None
    for _,ident,p in records:
        if ident == 0: nmts.append(p.hex())
        if ident == 0x601:
            assert pending is None
            pending = p
            if p[0] != 0x40: writes.append((int.from_bytes(p[1:3],'little'),p[3],int.from_bytes(p[4:],'little')))
        elif ident == 0x581:
            assert pending is not None and p[1:4] == pending[1:4]
            if pending[0] == 0x40:
                width = {0x43:4,0x4b:2,0x4f:1}[p[0]]
                readbacks[int.from_bytes(p[1:3],'little'),p[3]] = int.from_bytes(p[4:4+width],'little')
            else: assert p[0] == 0x60
            pending = None
    rpdo = [(0x1400,1,0x80000201),(0x1600,0,0),(0x1600,1,0x60400010),(0x1600,2,0x60ff0320),(0x1600,0,2),(0x1400,1,0x201)]
    tpdo = [(0x1801,1,0x80000281),(0x1a01,0,0),(0x1a01,1,0x60610008),(0x1a01,2,0x603f0020),(0x1a01,0,2),(0x1801,5,100),(0x1801,1,0x281)]
    expected = [(0x1017,0,500),(0x2000,0,1000)]+rpdo+tpdo+[(0x60ff,1,0),(0x60ff,2,0),(0x6040,0,6),(0x60ff,1,0),(0x60ff,2,0),(0x6040,0,0)]
    expected += [(i,s,0x60600008 if (i,s)==(0x1600,2) else v) for i,s,v in rpdo]
    expected += [(i,s,0 if i==0x1a01 or (i,s)==(0x1801,5) else v) for i,s,v in tpdo]
    expected += [(0x2000,0,0),(0x1017,0,0)]
    assert pending is None and writes == expected and nmts == ['8001','0101','8001']
    baseline = {(0x1017,0):0,(0x2000,0):0,(0x6040,0):0,(0x1400,1):0x201,(0x1600,0):2,(0x1600,1):0x60400010,(0x1600,2):0x60600008,(0x1801,1):0x281,(0x1801,2):255,(0x1801,5):0,(0x1a01,0):0,(0x1a01,1):0,(0x1a01,2):0,(0x60ff,1):0,(0x60ff,2):0,(0x603f,0):0,(0x606c,1):0,(0x606c,2):0,(0x606c,3):0}
    for key,value in baseline.items(): assert readbacks[key] == value,(key,readbacks.get(key))
    spec = importlib.util.spec_from_file_location('trace',ROOT/'scripts/test/analyze_control_hil_trace.py')
    module = importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    trace = module.analyze((BASE/'motion-target/application.log').read_text())
    assert trace['feedback_bad'] and len(trace['stops']) == 1 and trace['stops'][0]['cause'] == 2
    start_ns = next(t for t,i,p in trace['tx'] if i == 0x201 and p[2:] != bytes(4))
    stop_ns = trace['stops'][0]['ns']
    cycles = [d for t,d in trace['cycles'] if start_ns <= t <= stop_ns]
    assert cycles and all(d[8] > 0 and d[7] == 0 for d in cycles)
    assert cycles[-1][8] == 5 and cycles[-1][6] > 0 and cycles[-1][5] < 0
    raw = [row for row in trace['frames'] if start_ns <= row['ns'] <= stop_ns]
    last_second = [row for row in raw if row['ns'] >= stop_ns-1000000000]
    assert all(row['flags'] == 0 for row in raw)
    application = json.loads((BASE/'motion-target/result.json').read_text())
    assert application['application_exit'] == 1 and application['passed'] is False
    operator = json.loads((BASE/'operator-post-trial.json').read_text())
    assert operator['drive_power_off'] and operator['right_direction_and_stop_normal']
    assert operator['left_always_stationary'] and operator['no_abnormal_sound']
    result = {'status':'FAILED_post_stop_negative_feedback','matching_capture_frames':len(records),
              'trace_complete':True,'trace_records':trace['count'],'stop_cause':2,'selected_wheel':'right',
              'nonzero_window_ms':(stop-first)*1000,'right_target_range':[min(row[2] for row in moving),max(row[2] for row in moving)],
              'moving_feedback_tenths_rpm_range':[min(moving_speeds),max(moving_speeds)],
              'left_targets_and_feedback_zero':True,'negative_feedback':negative,
              'stable_zero_first_ms':(zeros[0]-stop)*1000,'stable_zero_span_ms':(zeros[-1]-zeros[0])*1000,
              'exact_volatile_writes':len(writes),'baseline_restored':True,
              'last_second_raw_ranges':{key:[min(row[key] for row in last_second),max(row[key] for row in last_second)] for key in ('ch1','ch3')},
              'input_interpretation':'Input ramped forward/left; final second CH1=304 and CH3=1800 constant. Right candidate remained positive with left zero through automatic cutoff. No pre-stop return-to-neutral or negative transmitted target.',
              'cause':'Drive/mechanical origin not established; independent captures agree on feedback bytes.',
              'operator':'Right direction/stop normal, left stationary, no abnormal sound, drive OFF confirmed in operator-post-trial.json',
              'timing_limit':'Candump timing only; JCAN timestamp is zero for every frame, so no independent JCAN latency measurement',
              'counter_limit':'Wrapper exits on application failure before can-after snapshot; no post-trial counter delta verified'}
    (BASE/'failure-analysis.json').write_text(json.dumps(result,indent=2)+chr(10))
    waveform = [{'after_stop_ms':(t-stop)*1000,'right_tenths_rpm':selected,'left_tenths_rpm':other}
                for t,selected,other in velocities if t >= stop]
    (BASE/'stop-waveform.json').write_text(json.dumps(waveform,indent=2)+chr(10))
    print(json.dumps(result,indent=2))


if __name__ == '__main__': main()
