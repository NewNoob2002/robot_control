"""Separate raw-byte silence from invalid-frame gaps and RF failsafe flags."""
from pathlib import Path
import json
import runpy

BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[4]
FIELDS = runpy.run_path(str(ROOT/'scripts/test/analyze_control_hil_trace.py'))['fields']


def check(log):
    """Require no received bytes during the full fault hold, bounded timeout and same-session restoration."""
    phases = {p['phase']:int(p['at_ns']) for p in
              (FIELDS(line) for line in log.splitlines() if line.startswith('event=recovery '))}
    ready, fault, release = (phases[k] for k in ('fault_ready','fault_observed','release_fault'))
    assert release-fault >= 5000000000
    batches, frames, cycles, boundaries, rejected = [], [], [], [], []
    for line in log.splitlines():
        if not line.startswith('event=trace_row '):
            continue
        row=FIELDS(line)
        stamp=int(row['ns']); data=[int(v) for v in row['fields'].split(',')]
        if row['kind']=='0':
            if data[2]: boundaries.append((stamp,data))
            batches.append((stamp,data))
        elif row['kind']=='1':
            frames.append((stamp,data))
        elif row['kind']=='2':
            rejected.append(stamp)
        elif row['kind']=='3':
            cycles.append((stamp,data))
    before=[(t,d) for t,d in frames if ready <= t <= fault]
    assert before and all(d[7]&12==0 for _,d in before), 'RF flags preceded withdrawal'
    last_frame=before[-1][0]
    assert len(boundaries) <= 1
    if boundaries:
        boundary, detail=boundaries[0]
        assert detail[2:5]==[3,0,0], 'Only empty partial-timeout boundary may be expected'
        assert ready < boundary <= fault and fault-boundary < 100000000
        recorded=[FIELDS(line) for line in log.splitlines() if line.startswith('event=uart_boundary ')]
        assert len(recorded)==1 and recorded[0]['kind']=='partial_timeout'
        assert int(recorded[0]['at_ns'])==boundary and int(recorded[0]['session'])==detail[1]
    else:
        assert 100000000 <= fault-last_frame <= 200000000, 'Not bounded valid-frame timeout'
    assert not any(d[3] for t,d in batches if fault <= t <= release), 'Raw bytes received during required silence hold'
    last_raw=max(t for t,d in batches if t <= fault and d[3])
    next_raw=min(t for t,d in batches if t > fault and d[3])
    assert ready <= last_raw <= fault < release < next_raw
    assert next_raw-last_raw > 1000000000
    first_restored=next((t,d) for t,d in frames if t > fault)
    restored_marks=[FIELDS(line) for line in log.splitlines() if line.startswith('event=uart_reconnected ')]
    assert restored_marks
    for mark in restored_marks:
        end=int(mark['at_ns']); start=int(mark['stable_since_ns'])
        assert release < start < end < release+45000000000
        assert end-start >= 1000000000
        assert not any(start <= t <= end for t in rejected), 'Rejected frame inside stable window'
        stable_cycles=[d for t,d in cycles if start <= t <= end]
        assert stable_cycles and all(d[5:7]==[0,0] and d[14]==0 and d[15]==2 for d in stable_cycles)
        stable_frames=[(t,d) for t,d in frames if start <= t <= end]
        assert stable_frames and stable_frames[-1][0]-start >= 1000000000
        assert all(d[7]&12==0 and d[5]<=500 for _,d in stable_frames)
        stamps=sorted({t for t,_ in stable_frames})
        assert all(b-a < 100000000 for a,b in zip(stamps,stamps[1:]))
    restored_at=int(restored_marks[-1]['at_ns'])
    assert release < restored_at < release+45000000000
    assert first_restored[0] <= restored_at
    expected_session=before[-1][1][1]+(1 if boundaries else 0)
    assert first_restored[0] > release and first_restored[1][1]==expected_session
    assert 0 <= first_restored[0]-next_raw <= 45000000000, 'Bytes without bounded valid-frame reacquisition'
    if boundaries:
        assert detail[1]==expected_session and 50000000 <= boundary-last_raw < 100000000
    else:
        assert last_raw==last_frame
    assert not any(d[3] for t,d in batches if last_raw < t < next_raw)
    withdrawn=[(t,d) for t,d in cycles if fault <= t <= release]
    assert withdrawn and all(d[4]==last_frame and d[14]==0 and d[15]==4 and d[9:13]==[0,0,0,0] for _,d in withdrawn)
    return dict(status='PASS_READER_RAW_BYTE_SILENCE', last_frame_ns=last_frame,
                fault_observed_ns=fault, release_fault_ns=release,
                last_raw_batch_ns=last_raw, first_restored_raw_ns=next_raw,
                raw_gap_ms=(next_raw-last_raw)/1e6, timeout_after_last_frame_ms=(fault-last_frame)/1e6,
                required_hold_ms=(release-fault)/1e6, raw_bytes_in_hold=0,
                expected_partial_boundaries=len(boundaries),
                stable_reconnection_count=len(restored_marks),
                stable_windows=[dict(start_ns=int(m["stable_since_ns"]),end_ns=int(m["at_ns"])) for m in restored_marks],
                rejected_candidates_retained=len(rejected),
                restored_session=expected_session,
                first_valid_after_reconnection_ms=(first_restored[0]-next_raw)/1e6,
                scope='No bytes delivered to Reader during hold; not a voltage-level or oscilloscope measurement')


def main():
    """Require the existing recovery/protocol oracle plus the stricter F5 raw-byte evidence."""
    log=(BASE/'recovery-target/application.log').read_text()
    silence=check(log)
    runpy.run_path(str(BASE/'analyze-recovery.py'))['main']()
    result=dict(status='PASS_F5_WIRE_PENDING_OPERATOR',silence=silence,
                recovery=json.loads((BASE/'recovery-analysis.json').read_text()))
    (BASE/'silence-analysis.json').write_text(json.dumps(result,indent=2)+chr(10))
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    main()
