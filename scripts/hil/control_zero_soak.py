#!/usr/bin/env python3
"""Supervise a single bounded RK3588 zero-target control session with diagnostics and a host lease."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import select
import shutil
import signal
import struct
import subprocess
import sys
import threading
import time
from phase6_zero_motion_soak import start_capture

STOP = threading.Event()
LEASE_ERROR = None
LAST_LEASE = 0.0
PACKET = struct.Struct('<19q')


def write_json(path, value):
    """Publish complete status without exposing a partially rewritten document."""
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2)+'\n')
    temporary.replace(path)


def digest(path):
    """Hash one artifact without loading it into memory."""
    with Path(path).open('rb') as stream:
        checksum = hashlib.sha256()
        for data in iter(lambda: stream.read(1024*1024), b''):
            checksum.update(data)
    return checksum.hexdigest()


def leases():
    """Revoke execution on EOF, invalid/abort message or10s silence."""
    global LAST_LEASE, LEASE_ERROR
    buffer = b''
    try:
        while not STOP.is_set():
            if time.monotonic()-LAST_LEASE > 10:
                raise RuntimeError('host lease expired')
            ready, _, _ = select.select([sys.stdin], [], [], 0.2)
            if not ready:
                continue
            data = os.read(sys.stdin.fileno(), 4096)
            if not data:
                raise RuntimeError('host disconnected')
            buffer += data
            if len(buffer) > 8192:
                raise RuntimeError('lease input exceeded bound')
            while b'\n' in buffer:
                line, buffer = buffer.split(b'\n', 1)
                if json.loads(line) != {'op': 'lease'}:
                    raise RuntimeError('host abort/invalid lease')
                LAST_LEASE = time.monotonic()
    except Exception as exc:
        LEASE_ERROR = str(exc)
        STOP.set()


def interface_state(interface):
    """Read existing interface statistics; never configure or transmit."""
    value = json.loads(subprocess.check_output(['ip','-j','-details','-statistics','link','show',interface],timeout=2))[0]
    assert value['linkinfo']['info_data']['bittiming']['bitrate'] == 500000
    assert 'UP' in value['flags'] and value['linkinfo']['info_data']['state'] == 'ERROR-ACTIVE', value
    return value


def resources(pid):
    """Collect process resource observations outside the control owner."""
    status = dict(line.split(':',1) for line in Path(f'/proc/{pid}/status').read_text().splitlines() if ':' in line)
    fields = Path(f'/proc/{pid}/stat').read_text().split(') ',1)[1].split()
    return dict(rss_kib=int(status['VmRSS'].split()[0]), threads=int(status['Threads']),
                fds=len(list(Path(f'/proc/{pid}/fd').iterdir())),
                cpu_ticks=int(fields[11])+int(fields[12]), clock_ticks=os.sysconf('SC_CLK_TCK'))


def stop_process(process, timeout):
    """Allow bounded cleanup, then force exit without claiming restoration."""
    if process is None or process.poll() is not None:
        return False
    process.terminate()
    try:
        process.wait(timeout=timeout)
        return False
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=2)
        return True


def run(args):
    """Verify exact identities before one-shot consumption; supervise captures and cleanup."""
    global LAST_LEASE
    config = json.loads(args.config.read_text())
    assert config['authorized'] is True and config['mode'] == 'zero-target'
    assert config['duration_s'] in (600,3600), 'only reviewed short/one-hour windows'
    assert Path('/etc/machine-id').read_text().strip() == config['machine_id']
    for name in ('elf','collector'):
        assert digest(config[name]) == config[name+'_sha256'], name
    assert config['standstill_tenths_rpm'] == 20
    assert config['interface'] == 'can0' and config['device'].startswith('/dev/')
    assert not args.output.exists(), 'never reuse a trial directory'
    args.output.mkdir(parents=True)
    (args.output/'CONSUMED').write_text(str(time.time()))
    shutil.copyfile(args.config, args.output/'authorization.json')
    for sig in (signal.SIGTERM,signal.SIGINT,signal.SIGHUP):
        signal.signal(sig,lambda *_: STOP.set())
    # A fresh live lease is required before starting CAN capture or the application.
    assert select.select([sys.stdin],[],[],5)[0], 'initial lease missing'
    assert json.loads(sys.stdin.readline()) == {'op':'lease'}
    LAST_LEASE = time.monotonic()
    thread=threading.Thread(target=leases,daemon=True)
    thread.start()
    child=collector=capture=None
    failure=None
    forced=False
    read_fd=write_fd=-1
    start=time.monotonic()
    previous_ticks=previous_sample=None
    try:
        before=interface_state(config['interface'])
        write_json(args.output/'can-before.json',before)
        assert shutil.disk_usage(args.output).free >= config['minimum_free_bytes']
        with (args.output/'trace.bin').open('wb') as raw, (args.output/'diagnostics.log').open('wb') as log, \
             (args.output/'application.log').open('wb') as app, (args.output/'candump.log').open('wb') as wire, \
             (args.output/'candump.stderr').open('wb') as wire_error, (args.output/'resources.jsonl').open('w') as metrics:
            capture=start_capture(config['interface'],wire,wire_error)
            # Let immediate option/bind failures surface before the application can send.
            deadline=time.monotonic()+0.5
            while time.monotonic()<deadline:
                assert not STOP.is_set(), LEASE_ERROR or 'operator stop'
                assert capture.poll() is None and not wire_error.tell(), 'capture startup failed'
                time.sleep(0.02)
            assert capture.poll() is None and not wire_error.tell(), 'capture startup failed'
            read_fd,write_fd=os.pipe2(os.O_CLOEXEC)
            os.set_blocking(write_fd,False)
            # A bounded larger FIFO absorbs normal scheduler bursts, never an unbounded queue.
            import fcntl
            fcntl.fcntl(write_fd,fcntl.F_SETPIPE_SZ,65536)
            collector=subprocess.Popen([config['collector']],stdin=read_fd,stdout=raw,stderr=log)
            os.close(read_fd);read_fd=-1
            environment=dict(os.environ,ROBOT_CONTROL_HIL_TRACE_FD=str(write_fd))
            child=subprocess.Popen([config['elf'],'--interface',config['interface'],'--device',config['device'],
                '--duration-ms',str(config['duration_s']*1000),'--zero-soak'],env=environment,pass_fds=(write_fd,),
                stdin=subprocess.DEVNULL,stdout=app,stderr=subprocess.STDOUT)
            os.close(write_fd);write_fd=-1
            next_sample=0
            cursor=0
            last_record=time.monotonic()
            with (args.output/'trace.bin').open('rb') as records:
                while child.poll() is None:
                    assert not STOP.is_set(), LEASE_ERROR or 'operator stop'
                    assert capture.poll() is None and collector.poll() is None, 'capture/diagnostic process exited'
                    assert time.monotonic()-start < config['duration_s']+90, 'total watchdog'
                    chunk=records.read(152*2048)
                    usable=len(chunk)//152*152
                    records.seek(usable-len(chunk),1)
                    if usable:
                        last_record=time.monotonic()
                    for row in struct.iter_unpack('<19q',chunk[:usable]):
                        assert row[0] == cursor, 'live record gap'
                        cursor+=1
                        if row[1] == 7:
                            cycles,missed,late,cycle=row[3:7]
                            assert late <= config['maximum_lateness_us'] and cycle <= config['maximum_cycle_us'], 'timing budget'
                            assert missed <= max(config['missed_startup_allowance'],cycles*config['missed_fraction']), 'missed-period budget'
                    assert time.monotonic()-last_record < 15, 'diagnostic stream stalled'
                    now=time.monotonic()
                    if now>=next_sample:
                        next_sample=now+1
                        try:
                            resource=resources(child.pid)
                        except (FileNotFoundError,KeyError):
                            if child.poll() is not None: break
                            raise
                        resource.update(control_ready='event=control_start ' in (args.output/'application.log').read_text(), elapsed_s=now-start, free_bytes=shutil.disk_usage(args.output).free,
                                        trace_bytes=raw.tell(), can_bytes=wire.tell(), lease_error=LEASE_ERROR)
                        resource['cpu_percent']=None if previous_ticks is None else \
                            100*(resource['cpu_ticks']-previous_ticks)/resource['clock_ticks']/(now-previous_sample)
                        previous_ticks,previous_sample=resource['cpu_ticks'],now
                        assert resource['rss_kib']<=config['maximum_rss_kib'] and resource['threads']<=config['maximum_threads']
                        assert resource['fds']<=config['maximum_fds'] and resource['free_bytes']>=config['reserve_free_bytes']
                        resource['can']=interface_state(config['interface'])
                        for group in ('rx','tx'):
                            for counter in ('errors','dropped'):
                                assert resource['can']['stats64'][group][counter] == before['stats64'][group][counter], 'CAN counter increased'
                        for counter in ('restarts','bus_error','error_warning','error_passive','bus_off'):
                            assert resource['can']['linkinfo']['info_xstats'][counter] == before['linkinfo']['info_xstats'][counter], 'CAN state/error counter increased'
                        metrics.write(json.dumps(resource)+'\n');metrics.flush()
                        write_json(args.output/'progress.json',resource)
                        print('PROGRESS '+json.dumps(resource),flush=True)
                    time.sleep(0.02)
            assert child.wait(timeout=2)==0, 'control process failed'
            assert collector.wait(timeout=5)==0, 'diagnostic finalization failed'
            assert capture.poll() is None, 'candump exited before finalization'
            capture.send_signal(signal.SIGINT)
            capture.wait(timeout=3)
            assert not wire_error.tell(), 'candump stderr'
            write_json(args.output/'can-after.json',interface_state(config['interface']))
    except BaseException as exc:
        failure=f'{type(exc).__name__}: {exc}'
    finally:
        forced=stop_process(child,15) or forced
        forced=stop_process(collector,3) or forced
        forced=stop_process(capture,3) or forced
        for descriptor in (read_fd,write_fd):
            if descriptor>=0: os.close(descriptor)
        STOP.set()
        thread.join(timeout=1)
    result=dict(status='AWAITING_OFFLINE_AUDIT' if failure is None and not forced else 'FAIL',
                error=failure,forced_kill=forced,lease_error=LEASE_ERROR,elapsed_s=time.monotonic()-start,
                process_rc=child.returncode if child else None,collector_rc=collector.returncode if collector else None,
                drive_power_off_confirmed=False)
    write_json(args.output/'result.json',result)
    write_json(args.output/'manifest.json',{p.name:digest(p) for p in args.output.iterdir() if p.is_file()})
    print('DONE '+json.dumps(dict(rc=0 if result['status']=='AWAITING_OFFLINE_AUDIT' else 1,lease_error=LEASE_ERROR)),flush=True)
    return 0 if result['status']=='AWAITING_OFFLINE_AUDIT' else 1


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    arguments=parser.parse_args()
    sys.exit(run(arguments))
