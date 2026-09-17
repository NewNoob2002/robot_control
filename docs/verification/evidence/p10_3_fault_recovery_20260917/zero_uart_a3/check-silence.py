"""Verify clean/partial silence acceptance and rejection of unrelated stream defects."""
from pathlib import Path
import runpy

BASE=Path(__file__).resolve().parent
check=runpy.run_path(str(BASE/'analyze-silence.py'))['check']


def row(kind,stamp,data):
    """Build one synthetic raw record for oracle tests only."""
    return f'event=trace_row kind={kind} ns={stamp} fields='+','.join(map(str,data+[0]*(16-len(data))))+'\n'


def fixture(partial):
    """Produce a one-second revoked hold with optional partial-frame session boundary."""
    fault=1160000000 if partial else 1200000000
    restored=2 if partial else 1
    text='event=recovery phase=fault_ready at_ns=1000000000\n'
    text+=f'event=recovery phase=fault_observed at_ns={fault}\n'
    text+='event=recovery phase=release_fault at_ns=6200000000\n'
    text+=row(0,1100000000,[1,1,0,42 if partial else 25,1])
    text+=row(1,1100000000,[1,1,0,1000,993,200,200,0])
    if partial:
        text+='event=uart_boundary kind=partial_timeout at_ns=1160000000 session=2\n'
        text+=row(0,fault,[2,2,3,0,0])
    text+=row(0,1500000000,[3,restored,0,0,0])
    text+=row(3,fault,[1,1,1,restored,1100000000,0,0,0,0,0,0,0,0,0,0,4])
    text+=row(0,6300000000,[4,restored,0,25,1])
    text+=row(1,6300000000,[4,restored,0,1000,993,200,200,0])
    text+='event=uart_reconnected at_ns=6300020000 authority=revoked\n'
    return text


assert check(fixture(False))['expected_partial_boundaries']==0
log=fixture(True)
assert check(log)['expected_partial_boundaries']==1
assert check(log)['required_hold_ms'] >= 5000
bad=[log+row(0,1600000000,[5,2,0,1,0]),
     log.replace('2,2,3,0,0','2,2,1,0,0'),
     log.replace('2,2,3,0,0','2,2,2,0,0'),
     log.replace('2,2,3,0,0','2,2,3,1,0'),
     log+row(0,1700000000,[6,3,3,0,0]),
     log.replace('kind=partial_timeout','kind=other'),
     log.replace('4,2,0,1000','4,3,0,1000'),
     log.replace('1000,993,200,200,0','1000,993,200,200,12'),
     log.replace('kind=1 ns=6300000000','kind=1 ns=53000000000')]
for value in bad:
    try:
        check(value)
    except (AssertionError,ValueError,StopIteration):
        pass
    else:
        raise AssertionError('Invalid F5 evidence accepted')
old=(BASE.parent/'zero_sbus_a1/recovery-target/application.log').read_text()
try:
    check(old)
except AssertionError:
    pass
else:
    raise AssertionError('Historical noisy F2 gap reused')
print('PASS: clean and partial silence; nine invalid fixtures and historical noisy F2 gap rejected')
