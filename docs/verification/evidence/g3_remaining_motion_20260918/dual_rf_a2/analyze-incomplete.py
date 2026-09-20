"""Audit late RF loss separately from the successful bounded motion and restoration."""
import importlib.util
import json
from pathlib import Path

BASE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('rf', BASE/'analyze-rf.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
log = (BASE/'physical-once/application.log').read_text()
result = audit.analyze(log)
summary = audit.fields(next(line for line in log.splitlines() if line.startswith('event=summary ')))
assert summary['primary_ok']==summary['restore_ok']=='1'
assert len(result['stops'])==1 and result['stops'][0]['cause']==2
assert not result['discontinuities'] and not result['feedback_bad']
feedback = audit.analyze_g3_feedback(result,'dual-positive',20)
first,zero = feedback['first_nonzero_ns'],feedback['first_zero_ns']
flags = [f for f in result['frames'] if f['flags']&12]
assert flags and flags[0]['ns']>zero
assert all(f['flags']==0 for f in result['frames'] if f['ns']<=zero)
wire = audit.parse_capture((BASE/'physical-once/candump.log').read_text())
independent = []
for line in (BASE/'jcan/session.jsonl').read_text().splitlines():
    event = json.loads(line)
    if event.get('event')=='frame':
        assert not any(event[k] for k in ('brs','extended','fd','remote'))
        independent.append((event['can_id'],bytes.fromhex(event['data_hex'])))
capture = audit.analyze_g3_captures(result,wire,independent,'dual-positive',20)
before = json.loads((BASE/'physical-once/can-before.json').read_text())
after = json.loads((BASE/'physical-once/can-final.json').read_text())
assert before['linkinfo']==after['linkinfo']
for side in ('rx','tx'):
    for key,value in before['stats64'][side].items():
        if key not in ('bytes','packets'):
            assert after['stats64'][side][key]==value
app = json.loads((BASE/'physical-once/result.json').read_text())
host = json.loads((BASE/'result.json').read_text())
operator = json.loads((BASE/'operator-off.json').read_text())
assert app['application_exit']==0 and not app['passed'] and not host['passed'] and host['capture_clean']
assert operator['response']=='OFF' and all(operator[key] for key in
    ('left_forward_and_stop_normal','right_forward_and_stop_normal','no_abnormal_sound',
     'drive_power_off','transmitter_off','receiver_continuously_powered','wiring_unchanged'))
report = dict(status='INCOMPLETE_RF_LOSS_AFTER_CUTOFF',summary=summary,feedback=feedback,capture=capture,
              motion_ms=(zero-first)/1e6,stop_verified_ms=(feedback['stable_verified_ns']-zero)/1e6,
              first_loss_after_zero_ms=(flags[0]['ns']-zero)/1e6,
              target_cue_to_received_loss_ms=(flags[0]['ns']-app['rf_off_cue_ns'])/1e6,
              flags_observed=sorted({f['flags'] for f in result['frames']}),
              sbus_frames=len(result['frames']),flagged_frames=len(flags),
              can_counters_unchanged=True,operator=operator,
              physical_button_timing_measured=False,
              scope='Normal motion/stop and restoration pass; RF-loss protection during motion remains unaccepted')
(BASE/'analysis.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+chr(10))
print(json.dumps(report,ensure_ascii=False,indent=2))
