"""Audit F6 signal causality on one target monotonic clock, retaining independent wire evidence."""
from pathlib import Path
import json
import runpy

BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[4]


def check(trace, event):
    """Reject late/missing signals, non-motion stimuli, resumed commands and failed revocation."""
    before, after = event['monotonic_before_ns'], event['monotonic_after_ns']
    assert event['sent'] and event['signal'] == 15 and 0 <= before <= after
    assert 300000000 <= before-event['positive_observed_ns'] < 1000000000
    targets = [(t,p) for t,i,p in trace['tx'] if i == 0x201]
    moving = [(t,p) for t,p in targets if p[2:] != bytes(4)]
    assert moving and all(p[0] == 15 and p[2:4] == bytes(2) and 0 < int.from_bytes(p[4:],'little',signed=True) <= 5 for _,p in moving)
    first = moving[0][0]
    zero = next(t for t,p in targets if t > first and p[2:] == bytes(4))
    assert first < before <= zero and zero-first < 2950000000, 'signal must cause first zero before cutoff'
    assert 0 <= zero-before <= 100000000, 'signal-to-zero conservative upper bound exceeds100ms'
    assert all(p[2:] == bytes(4) and p[0] not in (7,15) for t,p in targets if t >= zero)
    status = [(t,p) for t,i,p,_,_ in trace['rx'] if i == 0x181]
    assert sum(first <= t < before and int.from_bytes(p[6:8],'little',signed=True) > 20 for t,p in status) >= 2
    assert all(not int.from_bytes(p[:4],'little') & 0x80008000 for _,p in status), 'unexpected X1'
    assert len(trace['stops']) == 1 and trace['stops'][0]['cause'] == 4 and trace['stops'][0]['lifecycle_exit'] == 2
    assert trace['stops'][0]['ns'] >= before
    assert any(first <= t < before and d[12] > 0 and d[14] for t,d in trace['cycles'])
    revoked = [(t,d) for t,d in trace['cycles'] if t >= before]
    assert revoked and all(d[11:13] == [0,0] and not d[14] for _,d in revoked), 'source/command not revoked'
    assert not trace['discontinuities']
    assert all(f['flags'] & 12 == 0 for f in trace['frames'] if first <= f['ns'] <= trace['stops'][0]['ns'])
    return dict(first_nonzero_ns=first, first_zero_ns=zero, signal_before_ns=before,
                signal_after_ns=after, signal_to_zero_upper_us=(zero-before)/1000,
                signal_syscall_bracket_us=(after-before)/1000, no_restart=True,
                source_revoked=True, nonzero_window_ms=(zero-first)/1e6)


def main():
    """Combine the causal oracle, strict feedback checks, dual captures and complete restoration."""
    log = (BASE/'recovery-target/application.log').read_text()
    common = runpy.run_path(str(ROOT/'scripts/test/analyze_control_hil_trace.py'))
    trace = common['analyze'](log)
    event = json.loads((BASE/'recovery-target/sigterm-event.json').read_text())
    auth = json.loads((BASE/'authorization.json').read_text())
    run = json.loads((BASE/'recovery-target/result.json').read_text())
    assert event['artifact_sha256'] == auth['artifact_sha256'] and event['arguments'] == auth['arguments']
    assert event['pid'] == int((BASE/'recovery-target/application.pid').read_text())
    assert run['signal_sent'] and not run.get('forced_kill') and not run.get('abort_signal_sent') and not run.get('error')
    assert run['application_exit'] == 1, 'expected protected signal exit1'
    summary = next(common['fields'](line) for line in log.splitlines() if line.startswith('event=summary '))
    assert summary['restore_ok'] == '1' and 0 <= int(summary['shutdown_us']) <= 100000
    assert 'phase=runtime_stop ok=1' in log and 'event=motion_stop verified=1' in log
    result = check(trace,event)
    result['process_exit_after_signal_ms'] = (run['application_reaped_ns']-event['monotonic_before_ns'])/1e6
    assert 0 <= result['process_exit_after_signal_ms'] <= 12000
    result['internal_shutdown_us'] = int(summary['shutdown_us'])
    result['feedback_review_pending'] = trace['feedback_bad']
    if not trace['feedback_bad']:
        result['feedback'] = common['analyze_motion_feedback'](trace,20,True)
    runpy.run_path(str(BASE/'analyze-protocol.py'))['main']()
    result['protocol'] = json.loads((BASE/'protocol-analysis.json').read_text())
    for side in ('rx','tx'):
        prior = json.loads((BASE/'recovery-target/can-before.json').read_text())
        after = json.loads((BASE/'recovery-target/can-after.json').read_text())
        for name in ('errors','dropped','over_errors'):
            assert prior['stats64'][side].get(name,0) == after['stats64'][side].get(name,0)
    assert not any(after['linkinfo']['info_data'].get('berr_counter',{}).values())
    assert json.loads((BASE/'recovery-jcan-once/result.json').read_text())['passed']
    assert json.loads((BASE/'recovery-orchestration.json').read_text())['passed']
    result['status'] = 'PENDING_STOP_FEEDBACK_REVIEW' if trace['feedback_bad'] else 'PASS_F6_SOFTWARE_WIRE_PENDING_OPERATOR'
    (BASE/'sigterm-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
