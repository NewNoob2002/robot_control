"""Exercise F6 runner/oracle without hardware and without network access."""
from pathlib import Path
import ast
import copy
import json
import runpy
import signal
import subprocess
import sys
import tempfile
import time
from unittest.mock import patch
from operator_console import current_prompt, PROMPTS

BASE = Path(__file__).resolve().parent


def main():
    """Check rejected causal evidence, live prompts, direct-child identity and readiness gates."""
    for path in BASE.glob('*.py'):
        ast.parse(path.read_text(), filename=str(path))
    runner = runpy.run_path(str(BASE/'recovery-once.py'))
    due = runner['signal_due']
    sample = 'event=control elapsed_ms=1 left_tenths_rpm=0 right_tenths_rpm=50 enabled_samples=30\n'
    assert due('',1,None) == (None,False)
    assert due(sample,1,None) == (1,False)
    assert due(sample,1.2,1) == (1,False)
    assert due(sample,1.31,1) == (1,True)
    assert due(sample+'event=stop_cause',1.31,1) == (None,False)
    assert due(sample+sample.replace('right_tenths_rpm=50','right_tenths_rpm=0'),1.31,1) == (None,False)
    log = ''
    for name,_ in PROMPTS:
        log += name+': instruction\nWINDOW_REMAINING: 20\n'
        assert current_prompt(log)[0] == name
    log = log[:log.index('STOP_NOT_VERIFIED:')]
    assert current_prompt('STOP_NOT_VERIFIED: failed\nAPPLICATION_EXIT:')[0] == 'STOP_NOT_VERIFIED'
    for marker in ('APPLICATION_STOPPING:', 'APPLICATION_EXIT:', 'WINDOW_REMAINING: 0'):
        assert current_prompt(log+marker+'\nMOTION_READY: stale')[0] == 'STOP'
    host = runpy.run_path(str(BASE/'run-recovery.py'))
    with tempfile.TemporaryDirectory() as directory:
        host['main'].__globals__['BASE'] = Path(directory)
        with patch('subprocess.Popen',side_effect=AssertionError('spawn before readiness')):
            try:
                host['main']()
            except FileNotFoundError:
                pass
            else:
                raise AssertionError('missing readiness accepted')
        # A harmless child validates /proc parsing and the exact signal helper.
        binary = Path(sys.executable).resolve()
        args = ['-c','import time; time.sleep(10)']
        fn = runner['signal_child']
        fn.__globals__.update(BINARY=binary, ARGS=args, SHA='offline-test')
        child = subprocess.Popen([str(binary),*args])
        try:
            fn.__globals__['ARGS'] = ['wrong-arguments']
            try:
                fn(child,Path(directory),time.monotonic()-0.31)
            except AssertionError:
                pass
            else:
                raise AssertionError('wrong child command accepted')
            assert child.poll() is None and not (Path(directory)/'sigterm-event.json').exists()
            fn.__globals__['ARGS'] = args
            event = fn(child,Path(directory),time.monotonic()-0.31)
            assert event['sent'] and event['pid'] == child.pid
            assert child.wait(timeout=2) == -signal.SIGTERM
        finally:
            if child.poll() is None:
                child.terminate(); child.wait(timeout=2)
    check = runpy.run_path(str(BASE/'analyze-sigterm.py'))['check']
    move=bytes.fromhex('0f0000000500'); zero=bytes.fromhex('060000000000'); speed=bytes.fromhex('2714271400003200')
    active=[0]*16; active[12]=5; active[14]=1
    trace=dict(tx=[(100000000,0x201,move),(510000000,0x201,zero)],
               rx=[(200000000,0x181,speed,0,0),(300000000,0x181,speed,0,0)],
               cycles=[(300000000,active),(520000000,[0]*16)],
               stops=[dict(ns=530000000,cause=4,lifecycle_exit=2)],discontinuities=0,frames=[dict(ns=300000000,flags=0)])
    event=dict(monotonic_before_ns=500000000,monotonic_after_ns=500010000,positive_observed_ns=190000000,sent=True,signal=15)
    assert check(trace,event)['source_revoked']
    bad_cases=[]
    for change in ('early_zero','late_zero','resume','wrong_exit','no_motion','not_revoked','flags'):
        bad=copy.deepcopy(trace)
        if change == 'early_zero': bad['tx'][-1]=(400000000,0x201,zero)
        if change == 'late_zero': bad['tx'][-1]=(700000000,0x201,zero)
        if change == 'resume': bad['tx'].append((600000000,0x201,move))
        if change == 'wrong_exit': bad['stops'][0]['lifecycle_exit']=0
        if change == 'no_motion': bad['rx']=[]
        if change == 'not_revoked': bad['cycles'][-1][1][14]=1
        if change == 'flags': bad['frames'][0]['flags']=4
        try:
            check(bad,event)
        except (AssertionError,StopIteration):
            bad_cases.append(change)
        else:
            raise AssertionError(change+' accepted')
    print(json.dumps(dict(status='PASS',rejected=bad_cases,child_identity_signal='PASS',live_prompts='PASS',missing_readiness='PASS',physical_started=False),indent=2))


if __name__ == '__main__':
    main()
