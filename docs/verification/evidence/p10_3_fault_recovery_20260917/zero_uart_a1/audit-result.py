"""Audit the interrupted-frame F5 attempt without granting UART-silence recovery acceptance."""
from pathlib import Path
import json
import runpy

BASE=Path(__file__).resolve().parent
ROOT=BASE.parents[4]


def main():
    """Verify raw partial timeout, immediate revocation, zero targets, captures and restoration."""
    log=(BASE/'recovery-target/application.log').read_text()
    parser=runpy.run_path(str(ROOT/'scripts/test/analyze_control_hil_trace.py'))
    trace=parser['analyze'](log)
    batches=[]
    for line in log.splitlines():
        if line.startswith('event=trace_row '):
            row=parser['fields'](line)
            if row['kind']=='0':
                batches.append((int(row['ns']),[int(x) for x in row['fields'].split(',')]))
    discontinuities=[(t,d) for t,d in batches if d[2]]
    assert len(discontinuities)==1 and discontinuities[0][1][2:5]==[3,0,0]
    stamp,data=discontinuities[0]
    previous=[(t,d) for t,d in batches if t<stamp and d[3]]
    last_raw,raw=previous[-1]
    assert raw[3:5]==[42,1] and data[1]==raw[1]+1
    assert 50000000 <= stamp-last_raw < 100000000
    assert trace['discontinuities']==1 and not trace['feedback_bad']
    assert trace['stops'][0]['cause']==5 and len(trace['stops'])==1
    revoked=[(t,d) for t,d in trace['cycles'] if t>=stamp]
    assert revoked and all(d[14]==0 and d[9:13]==[0,0,0,0] for _,d in revoked)
    first_safe=next(t for t,i,p in trace['tx'] if t>=stamp and i==0x201 and p[0] in (0,2,6) and p[2:]==bytes(4))
    assert first_safe-stamp <= 100000000
    runpy.run_path(str(BASE/'analyze-recovery.py'))['main'](True)
    before=json.loads((BASE/'recovery-target/can-before.json').read_text())
    after=json.loads((BASE/'recovery-target/can-after.json').read_text())
    for side in ('rx','tx'):
        for key in ('errors','dropped','over_errors'):
            assert before['stats64'][side].get(key,0)==after['stats64'][side].get(key,0)
    assert not any(after['linkinfo']['info_data'].get('berr_counter',{}).values())
    operator=json.loads((BASE/'operator-post-trial.json').read_text())
    assert operator['drive_power_off'] and operator['wheels_stationary'] and operator['abnormal_sound'] is False
    summary=next(parser['fields'](line) for line in log.splitlines() if line.startswith('event=summary '))
    result=dict(status='INCOMPLETE_F5_PARTIAL_FRAME_DISCONTINUITY_PROTECTIVE_EXIT',
                source_fault=9,discontinuity='partial_timeout',trace_records=trace['count'],
                sbus_frames=len(trace['frames']),raw_bytes_in_last_nonempty_batch=42,
                complete_frames_in_last_batch=1,session_before=raw[1],session_after=data[1],
                last_raw_to_discontinuity_ms=(stamp-last_raw)/1e6,
                discontinuity_to_safe_rpdo_us=(first_safe-stamp)/1000,
                recovery_stages_completed=['fault_ready'],
                silence_timeout_recovery_accepted=False,can_counters_unchanged=True,
                protocol=json.loads((BASE/'incomplete-analysis.json').read_text()),
                summary=summary,operator=operator,runner_consumed=True,
                conclusion='Signal interruption left a partial frame; Reader revoked on partial timeout before100ms source timeout. Recovery observer accepts RF flags/timeout only, so terminated. No USB transport fault is recorded. No automatic retry.',
                signal_reconnection_confirmed=False)
    (BASE/'analysis.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+chr(10))
    print(json.dumps(result,ensure_ascii=False,indent=2))


if __name__=='__main__':
    main()
