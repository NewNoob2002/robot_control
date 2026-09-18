#!/usr/bin/env python3
"""Test the real supervisor cleanup/lease paths using inert child processes, without devices."""
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
from types import SimpleNamespace


def trial(collector, scenario):
    """Run one isolated module instance; stub only physical interface/capture boundaries."""
    spec=importlib.util.spec_from_file_location('soak',Path(__file__).parents[1]/'hil/control_zero_soak.py')
    soak=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(soak)
    original_popen=subprocess.Popen
    original_stdin=sys.stdin
    read_fd,write_fd=os.pipe()
    finished=threading.Event()
    errors=[]
    with tempfile.TemporaryDirectory() as tmp:
        root=Path(tmp)
        fake=root/'fake-control'
        fake.write_text('''#!/usr/bin/env python3
import os,struct,time,sys
fd=int(os.environ['ROBOT_CONTROL_HIL_TRACE_FD'])
def send(i,k,data):
 os.write(fd,struct.pack('<19q',i,k,time.monotonic_ns(),*(data+[0]*(16-len(data)))))
send(0,10,[1,20])
time.sleep(0.3)
''' + ("time.sleep(20)\n" if scenario=='abort' else "send(1,9,[0,0,20])\n") + ("sys.exit(1)\n" if scenario=='child_failure' else ''))
        fake.chmod(0o755)
        state={'flags':['UP'],'linkinfo':{'info_data':{'state':'ERROR-ACTIVE','bittiming':{'bitrate':500000}},
                'info_xstats':dict.fromkeys(('restarts','bus_error','error_warning','error_passive','bus_off'),0)},
                'stats64':{name:{'errors':0,'dropped':0} for name in ('rx','tx')}}
        soak.interface_state=lambda _:state
        def popen(command,**kwargs):
            """Replace only candump with a harmless sleeping process."""
            if command[0]=='candump':
                command=[sys.executable,'-c','import signal,time; signal.signal(signal.SIGINT,lambda *_:exit(0)); time.sleep(30)']
            return original_popen(command,**kwargs)
        def lease():
            """Feed live leases or a deliberate abort while the mock child is alive."""
            try:
                while not finished.is_set():
                    message={'op':'abort' if scenario=='abort' and (root/'result/progress.json').exists() else 'lease'}
                    os.write(write_fd,(json.dumps(message)+'\n').encode())
                    if message['op']=='abort': break
                    finished.wait(0.1)
            except OSError as exc:
                errors.append(str(exc))
        config=dict(authorized=True,mode='zero-target',duration_s=600,machine_id=Path('/etc/machine-id').read_text().strip(),
                    elf=str(fake),collector=str(collector),elf_sha256=soak.digest(fake),collector_sha256=soak.digest(collector),
                    standstill_tenths_rpm=20,interface='can0',device='/dev/mock',minimum_free_bytes=0,reserve_free_bytes=0,
                    maximum_lateness_us=100000,maximum_cycle_us=50000,missed_startup_allowance=10,missed_fraction=0.001,
                    maximum_rss_kib=262144,maximum_threads=4,maximum_fds=64)
        (root/'config.json').write_text(json.dumps(config))
        thread=threading.Thread(target=lease)
        try:
            subprocess.Popen=popen
            sys.stdin=io.TextIOWrapper(os.fdopen(read_fd,'rb',buffering=0))
            thread.start()
            started=time.monotonic()
            rc=soak.run(SimpleNamespace(config=root/'config.json',output=root/'result'))
            assert time.monotonic()-started<5
            result=json.loads((root/'result/result.json').read_text())
            assert rc==(0 if scenario=='complete' else 1),result
            assert not result['forced_kill']
            if scenario=='abort': assert result['lease_error'] and result['status']=='FAIL'
            assert (root/'result/CONSUMED').exists()
        finally:
            finished.set();thread.join(timeout=1)
            sys.stdin.close();sys.stdin=original_stdin
            os.close(write_fd)
            subprocess.Popen=original_popen
    assert not errors,errors


if __name__=='__main__':
    program=Path(sys.argv[1]).resolve()
    for case in ('complete','child_failure','abort'):
        trial(program,case)
    print('PASS: inert supervisor completion, child failure, live lease abort and bounded cleanup')
