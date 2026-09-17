"""Combine independent F6 signal/wire checks with explicit operator confirmation."""
from pathlib import Path
import json
import runpy

BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[4]


def main():
    """Accept this consumed F6 trial only, retaining original protective exit and raw observations."""
    runpy.run_path(str(BASE/'analyze-sigterm.py'))['main']()
    result = json.loads((BASE/'sigterm-analysis.json').read_text())
    assert result['status'] == 'PASS_F6_SOFTWARE_WIRE_PENDING_OPERATOR'
    operator = json.loads((BASE/'operator-post-trial.json').read_text())
    assert operator['raw_user_statement'] == 'OFF'
    assert all(operator[k] for k in ('right_direction_and_stop_normal','left_always_stationary',
                                    'drive_power_off','no_abnormal_sound','transmitter_stayed_on',
                                    'no_external_fault_stimulus','receiver_stayed_powered'))
    common = runpy.run_path(str(ROOT/'scripts/test/analyze_control_hil_trace.py'))
    log = (BASE/'recovery-target/application.log').read_text()
    trace = common['analyze'](log)
    summary = next(common['fields'](line) for line in log.splitlines() if line.startswith('event=summary '))
    right_stop = [int.from_bytes(p[6:8],'little',signed=True) for t,i,p,_,_ in trace['rx'] if i == 0x181 and t >= result['first_zero_ns']]
    result.update(status='PASS_F6_A1', operator=operator, trace_records=trace['count'],
                  sbus_frames=len(trace['frames']), summary=summary, feedback_bad=trace['feedback_bad'],
                  raw_right_stop_tenths_rpm=[min(right_stop),max(right_stop)],
                  target_runner_consumed=True, independent_capture_consumed=True,
                  can_counters_unchanged=True, expected_protective_exit=1,
                  timing_scope='Target monotonic signal-operation-to-successful-zero-send upper bound; JCAN confirms bytes/order, not independent latency',
                  scope='One unloaded right-positive moving SIGTERM; no loaded/reverse/dual-wheel/soak/kernel acceptance')
    (BASE/'acceptance.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n')
    print(json.dumps(result,indent=2,ensure_ascii=False))


if __name__ == '__main__':
    main()
