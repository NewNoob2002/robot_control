"""Audit this ground trial under the approved differential and stop-transient criteria."""
from pathlib import Path
import inspect
import json
import sys
import copy
import traceback
BASE=Path(__file__).resolve().parent
ROOT=next(p for p in BASE.parents if (p/'scripts/test/analyze_control_hil_trace.py').exists())
sys.path[:0]=[str(ROOT/'scripts/test'),str(ROOT/'scripts/hil')]
import analyze_control_hil_trace as audit
from phase6_zero_motion_soak import parse_capture

# Reuse every existing feedback check; alter only the reported verdict gate and bounded stop sign band.
source=inspect.getsource(audit.analyze_g3_feedback)
assert source.count("assert not result['feedback_bad'] and len(result['stops']) == 1")==1
assert source.count('else -zero_tolerance <= value*sign <= 75')==1
source=source.replace("assert not result['feedback_bad'] and len(result['stops']) == 1", "assert len(result['stops']) == 1")
source=source.replace('else -zero_tolerance <= value*sign <= 75','else (-75 if t >= zero else -zero_tolerance) <= value*sign <= 75')
namespace={}
exec(source,namespace)
base_feedback=namespace['analyze_g3_feedback']

def feedback(result,profile,tolerance=20):
    """Require the single observed post-zero raw +2.2rpm right-axis sample and all original guards."""
    assert profile=='differential-left' and result['right_sign']==-1 and tolerance==20
    report=base_feedback(result,profile,tolerance)
    first,zero,since=report['first_nonzero_ns'],report['first_zero_ns'],report['stable_since_ns']
    samples=[(t,int.from_bytes(p[4:6],'little',signed=True),int.from_bytes(p[6:8],'little',signed=True))
             for t,i,p,_,_ in result['rx'] if i==0x181]
    assert all(abs(l)<=75 and abs(r)<=75 for _,l,r in samples)
    excursions=[dict(ns=t,after_zero_ms=(t-zero)/1e6,left_raw_tenths=l,right_raw_tenths=r,
                     right_logical_tenths=-r,phase='bounded_stop')
                for t,l,r in samples if t>=zero and (l < -20 or r > 20)]
    assert len(excursions)==1 and excursions[0]['left_raw_tenths']==-4 and excursions[0]['right_raw_tenths']==22
    assert zero < excursions[0]['ns'] < since and 400 <= excursions[0]['after_zero_ms'] <= 425
    assert all(abs(l)<=20 and abs(r)<=20 for t,l,r in samples if t>=since)
    report.update(excursions=excursions,excursion_count=1,excursion_span_ms=0,excursion_peak_rpm=2.2,raw_samples_unchanged=True)
    return report

def main():
    """Retain original failures; audit raw application/wire clocks, exact restoration and operator observations."""
    log=(BASE/'physical-once/application.log').read_text();r=audit.analyze(log)
    assert r['feedback_bad'] and 'feedback_hard_bad=1' in log
    assert 'primary_ok=0 restore_ok=1' in log and 'operation=control_loop_command_envelope' in log
    assert r['stops'][0]['cause']==4 and r['discontinuities']==0
    rejected=[f for f in r['frames'] if f['ns']==r['stops'][0]['source_ns']]
    assert rejected and all(f['mapped'][2:]==(0,5) and f['flags']==0 for f in rejected)
    assert all(d[14] for _,d in r['cycles'] if d[11] or d[12])
    try: audit.analyze_g3_feedback(r,'differential-left',20)
    except AssertionError: (BASE/'original-feedback-audit-failure.txt').write_text(traceback.format_exc())
    else: raise AssertionError('Original feedback criterion unexpectedly passed')
    f=feedback(r,'differential-left',20)
    # Focused negative checks: the exception does not permit overspeed or repeated excursions.
    for mutation in ('overspeed','repeat'):
        changed=copy.deepcopy(r)
        ix=next(i for i,(t,ident,p,_,_) in enumerate(changed['rx']) if ident==0x181 and int.from_bytes(p[6:8],'little',signed=True)==22)
        row=changed['rx'][ix]
        if mutation=='repeat': changed['rx'].insert(ix+1,(row[0]+1,*row[1:]))
        else: changed['rx'][ix]=(row[0],row[1],row[2][:6]+(76).to_bytes(2,'little',signed=True),*row[3:])
        try: feedback(changed,'differential-left',20)
        except AssertionError: pass
        else: raise AssertionError('Invalid transient accepted: '+mutation)
    wire=parse_capture((BASE/'physical-once/candump.log').read_text());ind=[]
    for line in (BASE/'jcan/session.jsonl').read_text().splitlines():
        v=json.loads(line)
        if v.get('event')=='frame':
            assert not any(v[k] for k in ('brs','extended','fd','remote'))
            ind.append((v['can_id'],bytes.fromhex(v['data_hex'])))
    original=audit.analyze_g3_feedback
    try:
        audit.analyze_g3_feedback=feedback
        capture=audit.analyze_g3_captures(r,wire,ind,'differential-left',20)
    finally: audit.analyze_g3_feedback=original
    a=json.loads((BASE/'physical-once/can-before.json').read_text());z=json.loads((BASE/'physical-once/can-final.json').read_text());assert a['linkinfo']==z['linkinfo']
    for side in ('rx','tx'):
        for k,v in a['stats64'][side].items():
            if k not in ('bytes','packets'): assert z['stats64'][side][k]==v
    app=json.loads((BASE/'physical-once/result.json').read_text());host=json.loads((BASE/'result.json').read_text())
    assert app['application_exit']==1 and not app['passed'] and not host['passed'] and host['capture_clean']
    op=json.loads((BASE/'operator-off.json').read_text())
    assert op['response']=='OFF' and all(op[k] for k in ('left_forward_and_stop_normal','right_forward_and_stop_normal','no_abnormal_sound','drive_power_off'))
    report=dict(status='PASS_revised_differential_and_approved_stop_transient',feedback=f,capture=capture,
                motion_ms=(f['first_zero_ns']-f['first_nonzero_ns'])/1e6,
                feedback_stop_verified_ms=(f['stable_verified_ns']-f['first_zero_ns'])/1e6,
                can_counters_unchanged=True,operator=op,ground_observations=json.loads((BASE/'ground-observations.json').read_text()),
                classification='NORMAL_START_STOP_FEEDBACK_TRANSIENT',original_exit1_and_feedback_flags_preserved=True,
                online_guard_changed=False,mutation_checks=2)
    (BASE/'revised-acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+chr(10))
    print(json.dumps(report,ensure_ascii=False,indent=2))

if __name__=='__main__': main()
