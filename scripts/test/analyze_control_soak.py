#!/usr/bin/env python3
"""Stream-check zero-soak evidence without loading an hour of records into memory."""
import argparse
from collections import deque
import json
from pathlib import Path
import struct
from analyze_control_hil_trace import candidate

PACKET = struct.Struct('<19q')


def packets(path):
    """Yield complete ordered records; reject truncation and ordinal gaps."""
    with Path(path).open('rb') as stream:
        index = 0
        while data := stream.read(PACKET.size):
            assert len(data) == PACKET.size, 'truncated packet'
            row = PACKET.unpack(data)
            assert row[0] == index and row[2] >= 0, ('packet order', index)
            index += 1
            yield row


def analyze(path, duration_s):
    """Require exact zero commands, full source linkage, enabled operation and normal termination."""
    raw, recent = {}, deque()
    batch = None
    counts = {'records': 0, 'frames': 0, 'cycles': 0, 'rpdo': 0, 'enabled_cycles': 0}
    header = end = stop = final = None
    timing = []
    last_cycle = 0
    for ordinal, kind, ns, *data in packets(path):
        assert end is None, 'records after footer'
        counts['records'] += 1
        if ordinal == 0:
            assert kind == 10 and data[0] == 1 and data[1] == 20
            header = data
            continue
        assert header is not None and kind != 10
        if kind == 0:
            if batch is not None:
                assert batch['seen'] == batch['count']
            ident, session, discontinuity, raw_size, count = data[:5]
            assert 0 <= count <= 256 and 0 <= raw_size <= 256
            assert discontinuity == 0, 'Reader discontinuity'
            batch = dict(ident=ident, session=session, count=count, seen=0, ns=ns)
        elif kind in (1, 2):
            ident, session, event, ch1, ch3, ch6, ch7, flags = data[:8]
            assert batch and ident == batch['ident'] and session == batch['session']
            assert event == batch['seen'] and ns == batch['ns']
            batch['seen'] += 1
            # Startup resynchronization is retained; later rejection cannot count as clean endurance.
            assert kind == 1 or counts['enabled_cycles'] == 0, 'parser rejection after enable'
            if kind == 1:
                assert all(0 <= value <= 2047 for value in (ch1, ch3, ch6, ch7))
                assert flags & 12 == 0, 'SBUS lost/failsafe'
                key = (session, ns)
                if key not in raw:
                    recent.append(key)
                raw[key] = candidate(ch1, ch3)
                if len(recent) > 512:
                    del raw[recent.popleft()]
                counts['frames'] += 1
        elif kind == 3:
            assert data[0] > last_cycle
            last_cycle = data[0]
            assert not any(data[7:13]), 'nonzero candidate/selected/approved target'
            if data[15] in (2, 3):
                assert tuple(data[5:9]) == raw[data[3], data[4]], 'source linkage'
            if counts['enabled_cycles']:
                assert data[14] and data[15] == 3, 'authority lost'
            counts['enabled_cycles'] += bool(data[14] and data[15] == 3)
            counts['cycles'] += 1
        elif kind in (4, 5):
            ident, dlc = data[:2]
            assert 0 <= dlc <= 8
            payload = bytes(data[2:2+dlc])
            if kind == 5:
                assert data[10] == 16 and data[11] == 0, 'failed CAN send'
                assert ident in (0, 0x201, 0x601)
                if ident == 0x201:
                    assert dlc == 6 and payload[2:] == bytes(4), 'nonzero RPDO'
                    assert int.from_bytes(payload[:2], 'little') in (0, 2, 6, 7, 15)
                    counts['rpdo'] += 1
                elif ident == 0x601 and payload[0] != 0x40:
                    assert dlc == 8 and payload[0] in (0x2f, 0x2b, 0x23)
                    index = int.from_bytes(payload[1:3], 'little')
                    assert index in (0x6040,0x60ff,0x6060,0x2000,0x1017,0x1400,0x1600,0x1800,0x1a00,0x1801,0x1a01)
                    if index == 0x60ff:
                        assert payload[3] in (1,2,3) and payload[4:] == bytes(4)
                elif ident == 0:
                    assert payload in (b'\x80\x01',b'\x01\x01')
            elif ident == 0x181:
                assert dlc == 8
                assert all(-20 <= v <= 20 for v in struct.unpack('<hh',payload[4:])), 'feedback outside +/-2rpm'
        elif kind == 6:
            assert stop is None and data[0] == 0, 'protected/interrupted stop'
            stop = data
        elif kind == 7:
            assert not timing or ns-timing[-1][0] <= 5_000_000_000, 'diagnostic gap'
            timing.append((ns,data))
            if len(timing)>2:
                timing.pop(0)
            if data[7]:
                final = data
                assert data[8] == data[9] == 0, 'primary/cleanup failure'
        elif kind == 8:
            if not data[15]:
                assert data[12], 'unhealthy runtime'
        elif kind == 9:
            assert data[:3] == [0,0,20]
            end = data
        else:
            raise AssertionError(('unknown record',kind))
    assert header and end and stop and final and batch and batch['seen'] == batch['count']
    assert final[4] >= duration_s*1000 and final[5] == duration_s*1000
    assert counts['cycles'] >= duration_s*90 and counts['frames'] >= duration_s*100
    assert counts['enabled_cycles'] >= max(10,(duration_s-60)*90)
    assert counts['rpdo'] >= counts['cycles']
    return dict(status='PASS_SOFTWARE_EVIDENCE', **counts, elapsed_ms=final[4], missed_periods=final[1],
                maximum_lateness_us=final[2], maximum_cycle_us=final[3],
                scope='Continuous real-input zero-target control; dual wire capture/operator acceptance still required')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace',type=Path)
    parser.add_argument('--duration-s',type=int,required=True)
    args=parser.parse_args()
    print(json.dumps(analyze(args.trace,args.duration_s),indent=2))
