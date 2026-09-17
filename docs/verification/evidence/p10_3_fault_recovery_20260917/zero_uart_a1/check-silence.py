"""Exercise F5 oracle with silence, noise, RF loss and transport/session defects; no hardware."""
from pathlib import Path
import runpy

BASE=Path(__file__).resolve().parent
check=runpy.run_path(str(BASE/'analyze-silence.py'))['check']


def row(kind,stamp,data):
    """Build one padded synthetic raw trace row for the independent silence check."""
    values=data+[0]*(16-len(data))
    return f'event=trace_row kind={kind} ns={stamp} fields='+','.join(map(str,values))+'\n'


log='event=recovery phase=fault_ready at_ns=1000000000\n'
log+='event=recovery phase=fault_observed at_ns=1200000000\n'
log+='event=recovery phase=release_fault at_ns=2200000000\n'
log+=row(0,1100000000,[1,1,0,25,1])+row(1,1100000000,[1,1,0,1000,993,200,200,0])
log+=row(0,1500000000,[2,1,0,0,0])
log+=row(3,1200000000,[1,1,1,1,1100000000,0,0,0,0,0,0,0,0,0,0,4])
log+=row(0,2300000000,[3,1,0,25,1])+row(1,2300000000,[3,1,0,1000,993,200,200,0])
assert check(log)['raw_gap_ms']==1200
bad=[log+row(0,1600000000,[4,1,0,1,0]),
     log+row(0,2250000000,[4,1,0,1,0]),
     log.replace('1000,993,200,200,0','1000,993,200,200,12'),
     log.replace('3,1,0,1000','3,2,0,1000'),
     log.replace('2,1,0,0,0','2,1,1,0,0'),
     log.replace('at_ns=1200000000','at_ns=1190000000')]
for value in bad:
    try:
        check(value)
    except (AssertionError,ValueError,StopIteration):
        pass
    else:
        raise AssertionError('Invalid silence evidence accepted')
old=(BASE.parent/'zero_sbus_a1/recovery-target/application.log').read_text()
try:
    check(old)
except AssertionError:
    pass
else:
    raise AssertionError('Historical invalid-frame gap reused as F5 silence')
print('PASS: silence accepted; six invalid fixtures and historical F2 noisy gap rejected')
