"""Audit A6 execution and review items without turning the late RF loss into a pass."""
from pathlib import Path
import json
import runpy

BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[4]


def main():
    """Verify both captures, raw excursions, stop timing, recovery and operator disposition."""
    parser = runpy.run_path(str(ROOT / 'scripts/test/analyze_control_hil_trace.py'))
    log = (BASE / 'recovery-target/application.log').read_text()
    trace = parser['analyze'](log)
    header = next(parser['fields'](line) for line in log.splitlines() if line.startswith('event=trace_header '))
    assert trace['feedback_bad'] and not trace['discontinuities']
    assert header['feedback_hard_bad'] == '0' and header['feedback_reviews'] == '2'
    assert trace['stops'][0]['cause'] == 2 and len(trace['stops']) == 1
    targets = [(t,p) for t,i,p in trace['tx'] if i == 0x201]
    first = next(t for t,p in targets if p[2:] != bytes(4))
    zero = next(t for t,p in targets if t > first and p[2:] == bytes(4))
    assert 7950000000 <= zero - first < 8000000000
    assert all(p[2:] == bytes(4) for t,p in targets if t >= zero)
    losses = [f for f in trace['frames'] if f['ns'] > first and f['flags'] & 12]
    assert losses and losses[0]['ns'] > zero
    reviews = [parser['fields'](line) for line in log.splitlines() if line.startswith('event=feedback_review ')]
    speeds = [(t,int.from_bytes(p[6:8], 'little', signed=True)) for t,i,p,_,_ in trace['rx'] if i == 0x181]
    expected = [(t,v) for t,v in speeds if t >= zero and v < -20]
    assert expected == [(int(r['ns']),int(r['tenths_rpm'])) for r in reviews]
    assert [v for _,v in expected] == [-35,-38]
    assert all(r['verdict'] == 'pending' and r['phase'] == 'stopping' and r['wheel'] == 'right' for r in reviews)
    last_outside = max(t for t,v in speeds if t >= zero and abs(v) > 20)
    stable = [(t,v) for t,v in speeds if t > last_outside]
    assert stable[-1][0] - stable[0][0] >= 150000000
    assert stable[-1][0] - zero <= 1000000000 and all(abs(v) <= 20 for _,v in stable)
    assert 'event=motion_stop verified=1' in log and 'phase=restore ok=1' in log
    runpy.run_path(str(BASE / 'analyze-protocol.py'))['main']()
    before = json.loads((BASE/'recovery-target/can-before.json').read_text())
    after = json.loads((BASE/'recovery-target/can-after.json').read_text())
    for side in ('rx','tx'):
        for key in ('errors','dropped','over_errors'):
            assert before['stats64'][side].get(key,0) == after['stats64'][side].get(key,0)
    assert not any(after['linkinfo']['info_data'].get('berr_counter',{}).values())
    operator = json.loads((BASE/'operator-post-trial.json').read_text())
    assert all(operator[k] for k in ('drive_power_off','right_direction_and_stop_normal','left_always_stationary','no_abnormal_sound','receiver_stayed_powered'))
    assert operator['actual_operation']
    summary = next(parser['fields'](line) for line in log.splitlines() if line.startswith('event=summary '))
    result = dict(status='INCOMPLETE_F4_LOSS_AFTER_CUTOFF_WITH_PENDING_FEEDBACK_REVIEW',
                  artifact_sha256=json.loads((BASE/'authorization.json').read_text())['artifact_sha256'],
                  trace_records=trace['count'], sbus_frames=len(trace['frames']),
                  all_flags=sorted({f['flags'] for f in trace['frames']}),
                  moving_flags=sorted({f['flags'] for f in trace['frames'] if first <= f['ns'] <= zero}),
                  nonzero_ms=(zero-first)/1e6, loss_after_zero_ms=(losses[0]['ns']-zero)/1e6,
                  stop_cause='configured motion cutoff, not SBUS loss',
                  feedback_review=[dict(tenths_rpm=v, after_zero_ms=(t-zero)/1e6, verdict='PENDING') for t,v in expected],
                  stable_start_after_zero_ms=(stable[0][0]-zero)/1e6,
                  stable_verified_after_zero_ms=(stable[-1][0]-zero)/1e6,
                  application_summary=summary, protocol=json.loads((BASE/'protocol-analysis.json').read_text()),
                  counters_unchanged=True, operator=operator, runner_consumed=True,
                  interpretation='Selected stop feedback excursions with operator-normal stop; physical reversal or estimator artifact is not established by these samples alone. No automatic rerun.')
    (BASE/'analysis.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+chr(10))
    print(json.dumps(result,ensure_ascii=False,indent=2))


if __name__ == '__main__':
    main()
