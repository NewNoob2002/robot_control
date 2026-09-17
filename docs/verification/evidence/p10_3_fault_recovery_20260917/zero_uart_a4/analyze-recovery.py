"""Independently compare both captures and verify the exact zero-only actual zero-recovery ControlLoop trial."""
from pathlib import Path
from collections import Counter
import json
import re
import runpy
import argparse

BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[4]


def main(incomplete=False):
    """Reject missing captures, unexpected writes, nonzero velocity and incomplete restoration."""
    log = (BASE/'recovery-target/application.log').read_text()
    module = runpy.run_path(str(ROOT/'scripts/test/analyze_control_hil_trace.py'))
    if incomplete:
        phases = re.findall(r'event=recovery phase=(\w+) kind=(\w+)', log)
        assert phases == [('fault_ready', 'sbus')], phases
        assert 'operation=control_hil_recovery_incomplete' in log
        recovery = {'kind': 'sbus', 'status': 'INCOMPLETE', 'phases': phases}
    else:
        recovery = module['analyze_recovery'](log, 20)
    trace = module['analyze'](log)
    records = []
    for line in (BASE / 'recovery-target/candump.log').read_text().splitlines():
        fields = line.split()
        assert len(fields) >= 4 and fields[1] == 'can0', line
        assert fields[0].startswith('(') and fields[0].endswith(')'), line
        assert fields[3].startswith('[') and fields[3].endswith(']'), line
        timestamp, ident, dlc = float(fields[0][1:-1]), int(fields[2], 16), int(fields[3][1:-1])
        payload = bytes.fromhex(' '.join(fields[4:]))
        assert len(payload) == dlc
        records.append((timestamp, ident, payload))
    independent = []
    for line in (BASE / 'recovery-jcan-once/session.jsonl').read_text().splitlines():
        data = json.loads(line)
        if data.get('event') == 'frame':
            assert not any(data[key] for key in ('brs','extended','fd','remote'))
            independent.append((data['can_id'], bytes.fromhex(data['data_hex'])))
    assert independent == [(ident, payload) for _, ident, payload in records], 'captures differ'
    pending = None
    writes = []
    nmts = []
    readbacks = {}
    tpdo2_times = []
    rpdo_times = []
    enabled_feedback = []
    words = []
    for stamp, ident, payload in records:
        assert ident in (0,0x181,0x201,0x281,0x381,0x481,0x581,0x601,0x701), hex(ident)
        if ident == 0:
            assert payload in (b'\x80\x01',b'\x01\x01')
            nmts.append(payload.hex())
        elif ident == 0x181:
            assert len(payload) == 8 and all(abs(int.from_bytes(payload[o:o+2], 'little', signed=True)) <= 20 for o in (4,6))
            for half in (int.from_bytes(payload[:2],'little'),int.from_bytes(payload[2:4],'little')):
                assert half & 0x6f in (0,0x07,0x21,0x23,0x27,0x40,0x60)
            assert int.from_bytes(payload[2:4],'little') & 0x8000 == 0
            if incomplete or recovery['kind'] != 'x1': assert int.from_bytes(payload[:2],'little') & 0x8000 == 0
            if all(int.from_bytes(payload[i:i+2],'little') & 0x6f == 0x27 for i in (0,2)):
                enabled_feedback.append(stamp)
        elif ident == 0x201:
            assert len(payload) == 6 and payload[1:] == bytes(5)
            assert payload[0] in (0,2,6,7,15)
            words.append(payload[0])
            rpdo_times.append(stamp)
        elif ident == 0x281:
            assert payload == bytes([3,0,0,0,0])
            tpdo2_times.append(stamp)
        elif ident in (0x381,0x481):
            assert payload == b''
        elif ident == 0x701:
            assert payload in (bytes([5]),bytes([127]))
        elif ident == 0x601:
            assert len(payload) == 8 and pending is None
            index, sub = int.from_bytes(payload[1:3],'little'),payload[3]
            assert index in (0x1017,0x1400,0x1600,0x1800,0x1801,0x1a00,0x1a01,0x2000,0x200f,0x603f,0x6040,0x6041,0x605a,0x6060,0x6061,0x606c,0x60ff)
            pending = payload
            if payload[0] != 0x40:
                assert readbacks.get((0x605a,0)) == 5
                assert index in (0x1017,0x1400,0x1600,0x1801,0x1a01,0x2000,0x6040,0x60ff)
                writes.append((index,sub,int.from_bytes(payload[4:],'little')))
        elif ident == 0x581:
            assert pending is not None and len(payload) == 8 and payload[1:4] == pending[1:4]
            index,sub = int.from_bytes(payload[1:3],'little'),payload[3]
            if pending[0] == 0x40:
                assert payload[0] in (0x43,0x4b,0x4f)
                width = {0x43:4,0x4b:2,0x4f:1}[payload[0]]
                expected_width = 2 if index in (0x1017,0x2000,0x200f,0x6040,0x605a) or (index in (0x1400,0x1800,0x1801) and sub == 5) else 1 if index in (0x6060,0x6061) or (index in (0x1400,0x1800,0x1801) and sub == 2) or (index in (0x1600,0x1a00,0x1a01) and sub == 0) else 4
                assert width == expected_width
                value = int.from_bytes(payload[4:4+width],'little')
                readbacks[(index,sub)] = value
                if index == 0x606c:
                    data = payload[4:8]
                    assert all(abs(int.from_bytes(part, 'little', signed=True)) <= 20 for part in ([data[:2],data[2:]] if sub == 3 else [data]))
                if index in (0x60ff,0x603f):
                    assert value == 0
                if index in (0x6060,0x6061):
                    assert value == 3
            else:
                assert payload[0] == 0x60
            pending = None
    rpdo_setup = [(0x1400,1,0x80000201),(0x1600,0,0),(0x1600,1,0x60400010),
                  (0x1600,2,0x60ff0320),(0x1600,0,2),(0x1400,1,0x201)]
    rpdo_restore = [(i,s,0x60600008 if (i,s)==(0x1600,2) else v) for i,s,v in rpdo_setup]
    tpdo_setup = [(0x1801,1,0x80000281),(0x1a01,0,0),(0x1a01,1,0x60610008),
                  (0x1a01,2,0x603f0020),(0x1a01,0,2),(0x1801,5,100),(0x1801,1,0x281)]
    tpdo_restore = [(i,s,0 if i==0x1a01 or (i,s)==(0x1801,5) else v) for i,s,v in tpdo_setup]
    expected = [(0x1017,0,500),(0x2000,0,1000)] + rpdo_setup + tpdo_setup
    expected += [(0x60ff,1,0),(0x60ff,2,0),(0x6040,0,6)]
    expected += [(0x60ff,1,0),(0x60ff,2,0),(0x6040,0,0)] + rpdo_restore + tpdo_restore
    expected += [(0x2000,0,0),(0x1017,0,0)]
    assert readbacks[(0x605a,0)] == 5
    assert Counter((i,p) for _,i,p in trace['tx']) == Counter((i,p) for _,i,p in records if i in (0,0x201,0x601))
    assert pending is None and writes == expected and nmts == ['8001','0101','8001']
    for key,value in {(0x1017,0):0,(0x2000,0):0,(0x6040,0):0,(0x1400,1):0x201,
                      (0x1600,0):2,(0x1600,1):0x60400010,(0x1600,2):0x60600008,(0x1801,1):0x281,(0x1801,2):255,(0x1801,5):0,
                      (0x1a01,0):0,(0x1a01,1):0,(0x1a01,2):0}.items():
        assert readbacks[key] == value
    assert len(tpdo2_times) >= 35 and tpdo2_times[-1]-tpdo2_times[0] >= 1.9
    assert 6 in words and 7 in words and 15 in words
    assert words.index(6) < words.index(7) < words.index(15)
    assert len(enabled_feedback) >= 2 and len(rpdo_times) >= 100
    log = (BASE/'recovery-target/application.log').read_text()
    summary = re.search(r'event=summary cycles=(\d+) enabled_samples=(\d+).*primary_ok=' + ('0' if incomplete else '1') + r' restore_ok=1', log)
    assert summary and int(summary[2]) >= 10
    assert int(summary[1]) <= len(rpdo_times) <= int(summary[1])+3
    intervals = [(b-a)*1000 for a,b in zip(tpdo2_times,tpdo2_times[1:])]
    application = json.loads((BASE/'recovery-target/result.json').read_text())
    assert application['passed'] == (not incomplete) and application['application_exit'] == (1 if incomplete else 0)
    result={'status':'INCOMPLETE_SCENARIO_protocol_and_restoration_verified' if incomplete else 'PASS_protocol_and_restoration','matching_frames':len(records),
            'counts':{hex(k):v for k,v in Counter(i for _,i,_ in records).items()},
            'tpdo2_frames':len(tpdo2_times),'tpdo2_span_ms':(tpdo2_times[-1]-tpdo2_times[0])*1000,
            'tpdo2_interval_ms':[min(intervals),max(intervals)],'exact_volatile_writes':len(writes),
            'nmt':nmts,'all_targets_zero':True,'rpdo_frames':len(rpdo_times),'enabled_feedback':len(enabled_feedback),'enabled_samples':int(summary[2]),
            'baseline_restored':True,'operator':'Separate post-trial confirmation required; not inferred from protocol',
            'recovery':recovery,'scope':'Zero-only recovery scenario; physical operator confirmation remains separate'}
    (BASE/('incomplete-analysis.json' if incomplete else 'recovery-analysis.json')).write_text(json.dumps(result,indent=2)+chr(10))
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--incomplete", action="store_true", help="Audit F5 A4 partial-frame discontinuity exit; never grant recovery acceptance")
    main(parser.parse_args().incomplete)
