"""Independently compare both captures and verify the exact zero-motion PDO-layout trial."""
from pathlib import Path
from collections import Counter
import json

BASE = Path(__file__).resolve().parent


def main():
    """Reject missing captures, unexpected writes, nonzero velocity and incomplete restoration."""
    records = []
    for line in (BASE / 'diagnostics-target/candump.log').read_text().splitlines():
        fields = line.split()
        assert len(fields) >= 4 and fields[1] == 'can0', line
        assert fields[0].startswith('(') and fields[0].endswith(')'), line
        assert fields[3].startswith('[') and fields[3].endswith(']'), line
        timestamp, ident, dlc = float(fields[0][1:-1]), int(fields[2], 16), int(fields[3][1:-1])
        payload = bytes.fromhex(' '.join(fields[4:]))
        assert len(payload) == dlc
        records.append((timestamp, ident, payload))
    independent = []
    for line in (BASE / 'diagnostics-jcan-attempt2/session.jsonl').read_text().splitlines():
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
    for stamp, ident, payload in records:
        assert ident in (0,0x181,0x281,0x381,0x481,0x581,0x601,0x701), hex(ident)
        if ident == 0:
            assert payload in (b'\x80\x01',b'\x01\x01')
            nmts.append(payload.hex())
        elif ident == 0x181:
            assert len(payload) == 8 and payload[4:] == bytes(4)
            for half in (int.from_bytes(payload[:2],'little'),int.from_bytes(payload[2:4],'little')):
                assert half & 0x6f in (0,0x21,0x40,0x60)
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
            assert index in (0x1017,0x1800,0x1801,0x1a00,0x1a01,0x603f,0x6041,0x6060,0x6061,0x606c,0x60ff)
            pending = payload
            if payload[0] != 0x40:
                assert index in (0x1017,0x1801,0x1a01)
                writes.append((index,sub,int.from_bytes(payload[4:],'little')))
        elif ident == 0x581:
            assert pending is not None and len(payload) == 8 and payload[1:4] == pending[1:4]
            index,sub = int.from_bytes(payload[1:3],'little'),payload[3]
            if pending[0] == 0x40:
                assert payload[0] in (0x43,0x4b,0x4f)
                width = {0x43:4,0x4b:2,0x4f:1}[payload[0]]
                expected_width = 2 if index == 0x1017 or (index in (0x1800,0x1801) and sub == 5) else 1 if index in (0x6060,0x6061) or (index in (0x1800,0x1801) and sub == 2) or (index in (0x1a00,0x1a01) and sub == 0) else 4
                assert width == expected_width
                value = int.from_bytes(payload[4:4+width],'little')
                readbacks[(index,sub)] = value
                if index in (0x60ff,0x606c,0x603f):
                    assert value == 0
                if index in (0x6060,0x6061):
                    assert value == 3
            else:
                assert payload[0] == 0x60
            pending = None
    expected = [(0x1017,0,500),(0x1801,1,0x80000281),(0x1a01,0,0),(0x1a01,1,0x60610008),
                (0x1a01,2,0x603f0020),(0x1a01,0,2),(0x1801,5,100),(0x1801,1,0x281),
                (0x1801,1,0x80000281),(0x1a01,0,0),(0x1a01,1,0),(0x1a01,2,0),
                (0x1801,5,0),(0x1801,1,0x281),(0x1017,0,0)]
    assert pending is None and writes == expected and nmts == ['8001','0101','8001']
    for key,value in {(0x1017,0):0,(0x1801,1):0x281,(0x1801,2):255,(0x1801,5):0,
                      (0x1a01,0):0,(0x1a01,1):0,(0x1a01,2):0}.items():
        assert readbacks[key] == value
    assert len(tpdo2_times) >= 35 and tpdo2_times[-1]-tpdo2_times[0] >= 1.9
    intervals = [(b-a)*1000 for a,b in zip(tpdo2_times,tpdo2_times[1:])]
    application = json.loads((BASE/'diagnostics-target/result.json').read_text())
    assert application['passed'] and application['application_exit'] == 0
    result={'status':'PASS_protocol_and_restoration','matching_frames':len(records),
            'counts':{hex(k):v for k,v in Counter(i for _,i,_ in records).items()},
            'tpdo2_frames':len(tpdo2_times),'tpdo2_span_ms':(tpdo2_times[-1]-tpdo2_times[0])*1000,
            'tpdo2_interval_ms':[min(intervals),max(intervals)],'exact_volatile_writes':len(writes),
            'nmt':nmts,'no_controlword_target_rpdo_or_persistent_writes':True,
            'baseline_restored':True,'operator':'Confirmed no motion/no abnormal sound and drive power OFF',
            'scope':'TPDO2 zero-motion prerequisite only; full ControlLoop HIL remains open'}
    (BASE/'diagnostics-analysis.json').write_text(json.dumps(result,indent=2)+chr(10))
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
