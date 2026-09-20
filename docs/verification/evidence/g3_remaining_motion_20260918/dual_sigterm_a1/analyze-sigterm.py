"""Run the existing signed-motion and exact capture auditors, then verify trial disposition."""
import json
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parent
ROOT = next(p for p in BASE.parents if (p/'scripts/test/analyze_control_hil_trace.py').exists())
sys.path.insert(0,str(ROOT/'scripts/test'))
sys.path.insert(0,str(ROOT/'scripts/hil'))
from analyze_control_hil_trace import analyze, analyze_g3_feedback, analyze_g3_captures, fields
from phase6_zero_motion_soak import parse_capture


def check(trace, event):
    """Reject late/missing signals, non-motion stimuli, resumed commands and failed revocation."""
    before, after = event['monotonic_before_ns'], event['monotonic_after_ns']
    assert event['sent'] and event['signal'] == 15 and 0 <= before <= after
    assert 300000000 <= before-event['positive_observed_ns'] < 1000000000
    targets = [(t,p) for t,i,p in trace['tx'] if i == 0x201]
    moving = [(t,p) for t,p in targets if p[2:] != bytes(4)]
    assert moving and all(p[0] == 15 and 0 < int.from_bytes(p[2:4],'little',signed=True) <= 5 and int.from_bytes(p[4:],'little',signed=True) == -int.from_bytes(p[2:4],'little',signed=True) for _,p in moving)
    first = moving[0][0]
    zero = next(t for t,p in targets if t > first and p[2:] == bytes(4))
    assert first < before <= zero and zero-first < 2950000000, 'signal must cause first zero before cutoff'
    assert 0 <= zero-before <= 100000000, 'signal-to-zero conservative upper bound exceeds100ms'
    assert all(p[2:] == bytes(4) and p[0] not in (7,15) for t,p in targets if t >= zero)
    status = [(t,p) for t,i,p,_,_ in trace['rx'] if i == 0x181]
    assert sum(first <= t < before and int.from_bytes(p[4:6],'little',signed=True) > 20 and int.from_bytes(p[6:8],'little',signed=True) < -20 for t,p in status) >= 2
    assert all(not int.from_bytes(p[:4],'little') & 0x80008000 for _,p in status), 'unexpected X1'
    assert len(trace['stops']) == 1 and trace['stops'][0]['cause'] == 4 and trace['stops'][0]['lifecycle_exit'] == 2
    assert trace['stops'][0]['ns'] >= before
    assert any(first <= t < before and d[11] > 0 and d[12] > 0 and d[14] for t,d in trace['cycles'])
    revoked = [(t,d) for t,d in trace['cycles'] if t >= before]
    assert revoked and all(d[11:13] == [0,0] and not d[14] for _,d in revoked), 'source/command not revoked'
    assert not trace['discontinuities']
    assert all(f['flags'] & 12 == 0 for f in trace['frames'] if first <= f['ns'] <= trace['stops'][0]['ns'])
    return dict(first_nonzero_ns=first, first_zero_ns=zero, signal_before_ns=before,
                signal_after_ns=after, signal_to_zero_upper_us=(zero-before)/1000,
                signal_syscall_bracket_us=(after-before)/1000, no_restart=True,
                source_revoked=True, nonzero_window_ms=(zero-first)/1e6)


def main():
    """Preserve evidence and require independent protocol, timing, counters and operator acceptance."""
    log=(BASE/'physical-once/application.log').read_text()
    summary=fields(next(line for line in log.splitlines() if line.startswith('event=summary ')))
    assert summary['primary_ok']=='0' and summary['restore_ok']=='1', summary
    result=analyze(log)
    assert result['right_sign']==-1, 'wrong physical binding'
    assert result['discontinuities']==0 and all(d[14] for _,d in result['cycles'] if d[11] or d[12])
    event=json.loads((BASE/'physical-once/sigterm-event.json').read_text())
    auth=json.loads((BASE/'authorization.json').read_text())
    assert event['artifact_sha256']==auth['artifact_sha256'] and event['arguments']==auth['arguments']
    assert event['pid']==int((BASE/'physical-once/application.pid').read_text())
    signal_audit=check(result,event)
    profile='dual-positive'
    feedback=analyze_g3_feedback(result,profile,20)
    wire=parse_capture((BASE/'physical-once/candump.log').read_text())
    independent=[]
    for line in (BASE/'jcan/session.jsonl').read_text().splitlines():
        frame=json.loads(line)
        if frame.get('event')=='frame':
            assert not any(frame[k] for k in ('brs','extended','fd','remote'))
            independent.append((frame['can_id'],bytes.fromhex(frame['data_hex'])))
    capture=analyze_g3_captures(result,wire,independent,profile,20)
    before=json.loads((BASE/'physical-once/can-before.json').read_text())
    after=json.loads((BASE/'physical-once/can-final.json').read_text())
    assert before['linkinfo']==after['linkinfo']
    for side in ('rx','tx'):
        for key,value in before['stats64'][side].items():
            if key not in ('bytes','packets'): assert after['stats64'][side][key]==value
    app=json.loads((BASE/'physical-once/result.json').read_text())
    host=json.loads((BASE/'result.json').read_text())
    assert app['passed'] and app['application_exit']==1 and host['passed'] and app['signal_sent']
    assert not app.get('forced_kill') and not app.get('error')
    assert 0 <= app['application_reaped_ns']-event['monotonic_before_ns'] <= 12000000000
    assert 0 <= int(summary['shutdown_us']) <= 100000
    operator=json.loads((BASE/'operator-off.json').read_text())
    assert operator['response']=='OFF' and operator['left_forward_and_stop_normal'] and operator['right_forward_and_stop_normal']
    assert operator['no_abnormal_sound'] and operator['drive_power_off']
    raw=[tuple(int.from_bytes(p[i:i+2],'little',signed=True) for i in (4,6)) for _,ident,p,_,_ in result['rx'] if ident==0x181]
    report=dict(status='PASS',signal=signal_audit,right_sign=-1,profile=profile,summary=summary,feedback=feedback,capture=capture,
                motion_ms=(feedback['first_zero_ns']-feedback['first_nonzero_ns'])/1e6,
                stop_verified_ms=(feedback['stable_verified_ns']-feedback['first_zero_ns'])/1e6,
                raw_feedback_range_tenths_rpm=[[min(v[i] for v in raw),max(v[i] for v in raw)] for i in (0,1)],
                sbus_frames=len(result['frames']),trace_records=result['count'],can_counters_unchanged=True,
                operator=operator,scope='Dual-positive SIGTERM raised/unloaded only; original protective exit1 expected; no retry')
    (BASE/'analysis.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(report,ensure_ascii=False,indent=2))


if __name__=='__main__':
    main()
