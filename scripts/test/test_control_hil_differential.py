#!/usr/bin/env python3
"""Check straight-to-differential qualification evidence independently of hardware."""
import copy
import struct

from analyze_control_hil_trace import analyze_g3_feedback, candidate


def trace(right, polarity):
    """Supply raw command/feedback samples on one monotonic clock with bounded stop."""
    left, other = (5,4) if right else (4,5)
    def tx(t, a, b):
        """Encode a raw signed dual-target RPDO."""
        return t, 0x201, struct.pack('<Hhh',15,a,b*polarity)
    def rx(t, a, b):
        """Encode a raw dual-status TPDO."""
        return t, 0x181, struct.pack('<Ihh',0x00270027,a,b*polarity),16,0
    return dict(motion_mode=11 if right else 10,right_sign=polarity,zero_tolerance=20,
                feedback_bad=False,stops=[dict(cause=1)],
                tx=[tx(100000000,0,0),tx(200000000,3,3),tx(400000000,5,5),
                    tx(600000000,left,other),tx(700000000,left,other),tx(800000000,0,0)],
                rx=[rx(100000000,0,0),rx(300000000,30,30),rx(500000000,50,50),
                    rx(650000000,left*10,other*10),rx(750000000,left*10,other*10),
                    rx(850000000,0,0),rx(1000000000,0,0),rx(1050000000,0,0)])


def main():
    """Accept either polarity; reject straight-only, noncausal, wrong or insufficient differential evidence."""
    for right in (False,True):
        profile='differential-right' if right else 'differential-left'
        for polarity in (-1,1):
            original=trace(right,polarity)
            assert analyze_g3_feedback(original,profile)['differential_feedback_samples']==2
            for mutation in ('straight','equal_feedback','wrong_feedback','before_command','one_sample',
                             'after_zero','opposite_command','single_wheel','extended_from_turn','restart'):
                changed=copy.deepcopy(original)
                if mutation=='straight':
                    changed['tx']=[(t,i,struct.pack('<Hhh',15,5,5*polarity) if p[2:]!=bytes(4) else p)
                                   for t,i,p in changed['tx']]
                elif mutation in ('equal_feedback','wrong_feedback','before_command','one_sample','after_zero'):
                    for n in (3,4):
                        t,i,p,r,e=changed['rx'][n]
                        if mutation=='equal_feedback': p=struct.pack('<Ihh',0x00270027,50,50*polarity)
                        if mutation=='wrong_feedback': p=struct.pack('<Ihh',0x00270027,40 if right else 50,(50 if right else 40)*polarity)
                        if mutation=='before_command': t=550000000+(n-3)*10000000
                        if mutation=='one_sample' and n==4: p=struct.pack('<Ihh',0x00270027,50,50*polarity)
                        if mutation=='after_zero': t=810000000+(n-3)*10000000
                        changed['rx'][n]=(t,i,p,r,e)
                    changed['rx'].sort()
                elif mutation in ('opposite_command','single_wheel'):
                    t,i,p=changed['tx'][3]
                    a,b=(4,5) if right else (5,4)
                    if mutation=='single_wheel': a,b=0,5
                    changed['tx'][3]=(t,i,struct.pack('<Hhh',15,a,b*polarity))
                elif mutation=='extended_from_turn':
                    changed['tx'][1]=(1,*changed['tx'][1][1:])
                    changed['tx'][-1]=(3000000001,*changed['tx'][-1][1:])
                else:
                    changed['tx'].append((810000000,*changed['tx'][3][1:]))
                try:
                    analyze_g3_feedback(changed,profile)
                except AssertionError:
                    continue
                raise AssertionError('Accepted '+mutation)
        end=1160 if right else 840
        step=1 if right else -1
        path=[(1000,t) for t in range(993,1801)]+[(s,1800) for s in range(1000,end+step,step)]
        targets=[candidate(*raw)[2:] for raw in path]
        assert all((a,b)==(0,0) or (0<a<=5 and 0<b<=5 and (a>=b if right else a<=b)) for a,b in targets)
        assert targets[-1]==((5,4) if right else (4,5))
    print('PASS: both differential approaches/polarities and40 evidence mutations')


if __name__=='__main__':
    main()
