"""Check F5 stability oracle: healthy duration, faults, stale gaps and neutral qualification."""
from pathlib import Path
import runpy

BASE=Path(__file__).resolve().parent
check=runpy.run_path(str(BASE/'analyze-silence.py'))['check']


def row(kind,stamp,data):
    """Build one bounded raw trace row for offline oracle checks."""
    return f'event=trace_row kind={kind} ns={stamp} fields='+','.join(map(str,data+[0]*(16-len(data))))+'\n'


log='event=recovery phase=fault_ready at_ns=1000000000\n'
log+='event=recovery phase=fault_observed at_ns=1160000000\n'
log+='event=recovery phase=release_fault at_ns=6200000000\n'
log+='event=uart_boundary kind=partial_timeout at_ns=1160000000 session=2\n'
log+=row(0,1100000000,[1,1,0,42,1])+row(1,1100000000,[1,1,0,1000,993,200,200,0])
log+=row(0,1160000000,[2,2,3,0,0])
log+=row(3,1160000000,[1,1,1,2,1100000000,0,0,0,0,0,0,0,0,0,0,4])
for i in range(111):
    t=6300000000+i*10000000
    log+=row(0,t,[3+i,2,0,25,1])+row(1,t,[3+i,2,0,1000,993,200,200,0])
    log+=row(3,t,[3+i,1,1,2,t,0,0,0,0,0,0,0,0,0,0,2])
log+='event=uart_reconnected at_ns=7400020000 stable_since_ns=6350000000 authority=revoked\n'
assert check(log)['stable_reconnection_count']==1
bad=[log.replace('stable_since_ns=6350000000','stable_since_ns=7350000000'),
     log+row(2,6700000000,[0]),
     log.replace('2,2,3,0,0','2,2,1,0,0'),
     log+row(0,1500000000,[115,2,0,1,0]),
     log.replace('1000,993,200,200,0','1000,993,1800,200,0'),
     log+row(3,6800000000,[120,1,1,2,6800000000,0,0,0,0,0,0,0,0,0,0,4])]
for sample in bad:
    try:
        check(sample)
    except (AssertionError,StopIteration,ValueError):
        pass
    else:
        raise AssertionError('Invalid stable-input proof accepted')
old=(BASE.parent/'zero_uart_a3/recovery-target/application.log').read_text()
try:
    check(old)
except (AssertionError,KeyError):
    pass
else:
    raise AssertionError('A3 short healthy interval relabeled as stable1s')
print('PASS: continuous healthy1s accepted; six invalid proofs and historical A3 rejected')
