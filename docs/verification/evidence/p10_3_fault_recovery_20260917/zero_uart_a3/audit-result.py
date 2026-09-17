"""Audit raw silence and repeated post-reconnection rejection without granting full recovery acceptance."""
from pathlib import Path
import json
import runpy

BASE=Path(__file__).resolve().parent
ROOT=BASE.parents[4]


def main():
    """Verify raw gap, exact zero protocol, retained rejection and physical OFF disposition."""
    parser=runpy.run_path(str(ROOT/'scripts/test/analyze_control_hil_trace.py'))
    log=(BASE/'recovery-target/application.log').read_text()
    trace=parser['analyze'](log)
    silence=runpy.run_path(str(BASE/'analyze-silence.py'))['check'](log)
    runpy.run_path(str(BASE/'analyze-recovery.py'))['main'](True)
    restored=int(next(parser['fields'](line)['at_ns'] for line in log.splitlines() if line.startswith('event=uart_reconnected ')))
    rejected=[]
    for line in log.splitlines():
        if line.startswith('event=trace_row '):
            row=parser['fields'](line)
            if row['kind']=='2' and int(row['ns'])>restored:
                rejected.append(int(row['ns']))
    assert len(rejected)==2 and 400000000 <= rejected[0]-restored < 600000000
    assert len(trace['stops'])==1 and trace['stops'][0]['cause']==5
    assert not trace['feedback_bad'] and trace['discontinuities']==1
    after_restored=[(t,d) for t,d in trace['cycles'] if t>=restored]
    assert after_restored and all(d[5:13]==[0]*8 and d[14]==0 for _,d in after_restored)
    before=json.loads((BASE/'recovery-target/can-before.json').read_text())
    after=json.loads((BASE/'recovery-target/can-after.json').read_text())
    for side in ('rx','tx'):
        for key in ('errors','dropped','over_errors'):
            assert before['stats64'][side].get(key,0)==after['stats64'][side].get(key,0)
    assert not any(after['linkinfo']['info_data'].get('berr_counter',{}).values())
    operator=json.loads((BASE/'operator-post-trial.json').read_text())
    assert operator['drive_power_off'] and operator['wheels_stationary'] and operator['abnormal_sound'] is False
    result=dict(status='INCOMPLETE_F5_REJECTED_AFTER_HEALTHY_RECONNECTION',
                silence=silence, initial_withdrawal='partial_timeout/discontinuity, not the100ms valid-frame timeout',
                restored_ns=restored,rejected_candidates_after_restored=len(rejected),
                first_rejected_after_restored_ms=(rejected[0]-restored)/1e6,
                source_fault=2,phase=2,trace_records=trace['count'],sbus_frames=len(trace['frames']),
                recorded_axes_neutral_and_authority_revoked_after_reconnect=True,
                protocol=json.loads((BASE/'incomplete-analysis.json').read_text()),
                can_counters_unchanged=True,operator=operator,runner_consumed=True,
                conclusion='Raw-byte silence and5s hold verified. After healthy Disabled input, new rejected candidates triggered protected exit. Not window exhaustion. Full challenge/rearm recovery incomplete; physical cause of stream defect not established. No retry.')
    (BASE/'analysis.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+chr(10))
    print(json.dumps(result,ensure_ascii=False,indent=2))


if __name__=='__main__':
    main()
