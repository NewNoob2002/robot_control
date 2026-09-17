"""Explicit user-approved ±2rpm reassessment; never rewrite original ±1.5rpm evidence."""
from pathlib import Path
import hashlib
import json
import runpy

BASE = Path(__file__).resolve().parent
TRIAL = BASE.parent/'motion_sbus_a5'
ROOT = BASE.parents[4]


def main():
    """Verify immutable captures, revised feedback, original SBUS causality and operator disposition."""
    for line in (TRIAL/'SHA256SUMS').read_text().splitlines():
        expected, name = line.split('  ', 1)
        assert hashlib.sha256((TRIAL/name).read_bytes()).hexdigest() == expected, name
    previous = json.loads((TRIAL/'failure-analysis.json').read_text())
    assert previous['status'] == 'FAILED_F4_FEEDBACK_LIMIT_WITH_VALID_SBUS_WITHDRAWAL'
    assert previous['original_band_tenths_rpm'] == 15 and previous['feedback_bad']
    assert len(previous['raw_outliers']) == 1 and previous['raw_outliers'][0][1:] == [0,-16]
    log = (TRIAL/'recovery-target/application.log').read_text()
    analyze = runpy.run_path(str(ROOT/'scripts/test/analyze_control_hil_trace.py'))
    trace = analyze['analyze'](log)
    assert 'zero_feedback_tenths_rpm=15 ' in log and 'motion_window_ms=8000 ' in log
    assert trace['feedback_bad'] and trace['discontinuities'] == 0
    causal = runpy.run_path(str(TRIAL/'analyze-sbus.py'))['check'](trace, 3, 8000)
    feedback = analyze['analyze_motion_feedback'](trace, 20, True, 8000)
    try:
        analyze['analyze_motion_feedback'](trace, 15, True, 8000)
    except AssertionError:
        pass
    else:
        raise AssertionError('Original15-band failure disappeared')
    runpy.run_path(str(BASE/'analyze-a5-protocol.py'))['main']()
    operator = json.loads((TRIAL/'operator-post-trial.json').read_text())
    for key in ('drive_power_off','right_direction_and_stop_normal','left_always_stationary',
                'no_abnormal_sound','transmitter_off','receiver_stayed_powered'):
        assert operator[key]
    assert operator['actual_operation']
    result = dict(status='PASS_F4_A5_UNDER_EXPLICIT_REVISED_20_BAND',
                  measured_artifact_sha256='38800cfe2e4f9d7de1c338569a07b387ff15ca77b25e4737a4391c9ddaa7f513',
                  recorded_tolerance_tenths_rpm=15,revised_tolerance_tenths_rpm=20,
                  original_feedback_bad_preserved=True,original_failed_verdict_preserved=True,
                  causal=causal,feedback=feedback,
                  protocol=json.loads((BASE/'a5-protocol-analysis.json').read_text()),
                  operator=operator,new_hardware_trial=False,
                  limitation='Retrospective acceptance of unchanged A5 captures, not execution of the new20-default artifact')
    (BASE/'a5-revised-analysis.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+chr(10))
    print(json.dumps(result,ensure_ascii=False,indent=2))


if __name__ == '__main__':
    main()
