"""Replay gradual manual paths against the unchanged signed qualification contract."""
import hashlib
import json
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parent
ROOT = next(p for p in BASE.parents if (p/'scripts/test/analyze_control_hil_trace.py').exists())
sys.path.insert(0,str(ROOT/'scripts/test'))
from analyze_control_hil_trace import candidate


def allowed(left, right, side):
    """Independent reference for the existing strict positive differential target gate."""
    return (left,right)==(0,0) or (1<=left<=5 and 1<=right<=5 and
                                  (left<right if side=='left' else left>right))


def main():
    """Check both manual orders and simultaneous ramps; preserve the first rejection."""
    frozen = json.loads((BASE.parent/'g3_polarity_fix_20260918/compiled-inputs.json').read_text())['sha256']
    assert all(hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==sha for name,sha in frozen.items())
    results = []
    for side,end in (('left',840),('right',1160)):
        step = -1 if end<1000 else 1
        paths = {
            'steering_then_throttle': [(x,993) for x in range(1000,end+step,step)] + [(end,y) for y in range(994,1801)],
            'throttle_then_steering': [(1000,y) for y in range(993,1801)] + [(x,1800) for x in range(1000+step,end+step,step)],
            'simultaneous_linear_ramp': [(1000+step*(160*n//807),993+n) for n in range(808)],
        }
        for name,path in paths.items():
            final = candidate(*path[-1])[2:]
            assert final==((4,5) if side=='left' else (5,4)) and allowed(*final,side)
            rejected = [(index,raw,candidate(*raw)[2:]) for index,raw in enumerate(path)
                        if not allowed(*candidate(*raw)[2:],side)]
            assert rejected, 'This path no longer reproduces the qualification blocker'
            index,raw,targets = rejected[0]
            assert targets in ((0,3),(3,0),(3,3))
            results.append(dict(side=side,path=name,samples=len(path),final_targets=final,
                                first_rejected=dict(index=index,ch1=raw[0],ch3=raw[1],targets=targets),
                                rejected_samples=len(rejected)))
    report = dict(status='BLOCKED_MANUAL_DIFFERENTIAL_TRANSITION',paths=results,
                  compiled_source_inputs_verified=len(frozen),hardware_run=False,runtime_changed=False,
                  limitation='Discrete gradual input paths only; not a proof about all sampled human gestures',
                  consequence='First invalid enabled candidate stops ControlLoop before submission; later valid endpoint cannot resume')
    (BASE/'differential-path-check.json').write_text(json.dumps(report,indent=2)+chr(10))
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    main()
