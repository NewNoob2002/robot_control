"""Combine independent F5 wire/stability checks with counters and explicit physical disposition."""
from pathlib import Path
import json
import runpy

BASE=Path(__file__).resolve().parent
ROOT=BASE.parents[4]


def main():
    """Accept only the recorded one-shot raw silence and zero-recovery scenario with operator OFF."""
    runpy.run_path(str(BASE/'analyze-silence.py'))['main']()
    result=json.loads((BASE/'silence-analysis.json').read_text())
    assert result['status']=='PASS_F5_WIRE_PENDING_OPERATOR'
    before=json.loads((BASE/'recovery-target/can-before.json').read_text())
    after=json.loads((BASE/'recovery-target/can-after.json').read_text())
    for side in ('rx','tx'):
        for key in ('errors','dropped','over_errors'):
            assert before['stats64'][side].get(key,0)==after['stats64'][side].get(key,0)
    assert not any(after['linkinfo']['info_data'].get('berr_counter',{}).values())
    operator=json.loads((BASE/'operator-post-trial.json').read_text())
    assert all(operator[k] for k in ('drive_power_off','wheels_stationary','signal_only_stimulus','usb_ground_power_unchanged','transmitter_stayed_on'))
    assert operator['abnormal_sound'] is False
    assert json.loads((BASE/'recovery-orchestration.json').read_text())['passed']
    assert json.loads((BASE/'recovery-jcan-once/result.json').read_text())['passed']
    parser=runpy.run_path(str(ROOT/'scripts/test/analyze_control_hil_trace.py'))
    log=(BASE/'recovery-target/application.log').read_text()
    trace=parser['analyze'](log)
    summary=next(parser['fields'](line) for line in log.splitlines() if line.startswith('event=summary '))
    result.update(status='PASS_F5_A4',operator=operator,can_counters_unchanged=True,
                  trace_records=trace['count'],sbus_frames=len(trace['frames']),
                  feedback_bad=trace['feedback_bad'],summary=summary,runner_consumed=True,
                  initial_withdrawal='Immediate partial-timeout discontinuity; not a claimed100ms timeout trigger',
                  measurement_scope='Reader raw-byte silence plus zero-output recovery; no voltage-level measurement or moving test',
                  historical_attempts='A1/A2/A3 remain incomplete; no original record reclassified')
    (BASE/'acceptance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+chr(10))
    print(json.dumps(result,ensure_ascii=False,indent=2))


if __name__=='__main__':
    main()
