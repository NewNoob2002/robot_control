"""Single authorized manual-rotation attempt; Rust JCAN owns all physical TX."""
import json
import queue
import select
import shlex
import subprocess
import sys
import threading
import time
from pathlib import Path

root = Path('docs/verification/evidence/p6_review_hil_20260910_round2')
m = json.loads((root / 'manual_rotation_manifest.json').read_text())
out = root / 'manual_attempt_2'
out.mkdir(exist_ok=False)
(out / 'runner.py').write_bytes(Path(__file__).read_bytes())
(out / 'safety_preflight.json').write_text(json.dumps({'authorization':'User: 授权按相同报文清单重新执行','manifest':m,'operator_ready':True,'recovery':'One reserved pre-operational frame, no retry; shutdown JCAN; bounded passive capture exits'},indent=2))
r = {'sent':[], 'reads':[], 'passed':False}
cli='/home/gtc/Desktop/workspace/JCAN/target/release/jcan'
remote_code = r'''
import subprocess,sys,time,os,signal,select,json
from pathlib import Path
assert Path('/etc/machine-id').read_text().strip()=='6923ab3301fb4a8d816759b04ec6bf0a'
def state():
 return json.loads(subprocess.check_output(['ip','-j','-details','-statistics','link','show','can0']))[0]
a=state(); d=a['linkinfo']['info_data']
assert 'UP' in a['flags'] and a['mtu']==16 and d['state']=='ERROR-ACTIVE' and d['bittiming']['bitrate']==500000
assert all(v==0 for v in d.get('berr_counter',{}).values())
print('CAN_BEFORE '+json.dumps(a),flush=True)
p=subprocess.Popen(['candump','-ta','-e','-n','10000','can0,0:0,#FFFFFFFF'],stdout=sys.stdout,stderr=sys.stderr)
try:
 time.sleep(.2)
 assert p.poll() is None
 assert any(os.readlink(f).startswith('socket:') for f in Path('/proc',str(p.pid),'fd').iterdir())
 print('CAPTURE_READY',flush=True)
 select.select([sys.stdin],[],[],70)
finally:
 if p.poll() is None:p.send_signal(signal.SIGINT)
 p.wait(timeout=3)
print('CAN_AFTER '+json.dumps(state()),flush=True)
assert p.returncode==0
'''
remote=jcan=None
started=False
cleanup_sent=False
number=0
def reader(stream,q,path):
    """Record raw output and monotonic receipt time before dispatch."""
    with path.open('w') as f:
        for line in stream:
            f.write(line);f.flush();q.put((time.monotonic(),line))
    q.put((time.monotonic(),None))
try:
    print('ARMED: awaiting GO before discovery or physical TX (120 seconds)',flush=True)
    assert select.select([sys.stdin],[],[],120)[0] and sys.stdin.readline().strip()=='GO','pre-transmission GO timeout'
    for args in [['self-test'],['scan'],['--serial',m['adapter_serial'],'config-get']]:
        p=subprocess.run([cli,'--json']+args,capture_output=True,text=True,timeout=8)
        (out/(args[-1]+'.json')).write_text(p.stdout)
        assert p.returncode==0 and not p.stderr,(p.returncode,p.stderr)
        v=json.loads(p.stdout);assert v['ok'] and not v.get('warnings'),v
        if args==['scan']:assert any(x['serial']==m['adapter_serial'] for x in v['data'])
        if args[-1]=='config-get':
            assert v['data']['can_speed']=='0C'
            r['config_before']=v['data']
    remote=subprocess.Popen(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=5','robot-dev','python3 -c '+shlex.quote(remote_code)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    rq=queue.Queue()
    threading.Thread(target=reader,args=(remote.stdout,rq,out/'rk3588_capture.log'),daemon=True).start()
    deadline=time.monotonic()+8
    while True:
        _,line=rq.get(timeout=max(.001,deadline-time.monotonic()))
        assert line is not None,'capture EOF'
        if line.strip()=='CAPTURE_READY':break
    jcan=subprocess.Popen([cli,'--json','--serial',m['adapter_serial'],'session','--mode','normal','--receive'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    jq=queue.Queue()
    threading.Thread(target=reader,args=(jcan.stdout,jq,out/'jcan_session.jsonl'),daemon=True).start()
    tpdo=[]
    def event(timeout):
        """Reject transport faults and record TPDOs without changing the drive."""
        assert remote.poll() is None,'independent capture stopped'
        stamp,line=jq.get(timeout=timeout)
        assert line is not None,'JCAN EOF'
        v=json.loads(line)
        assert v.get('ok') is not False and not v.get('warnings'),v
        assert v.get('event') not in ('warning','error','disconnected','reconnecting'),v
        if v.get('event')=='frame':
            assert not v['extended'] and not v['fd'] and not v['remote'],v
            if v['can_id']==0x181:
                data=bytes.fromhex(v['data_hex']);assert len(data)==8,v
                status=int.from_bytes(data[:4],'little')
                assert all(((status>>shift)&0x6f)==0x21 for shift in [0,16]),v
                tpdo.append({'monotonic':stamp,'data':v['data_hex']})
            assert v['can_id']!=0x81,'EMCY observed'
        return v
    seen=set();deadline=time.monotonic()+5
    while not {'connected','session_started'} <= seen:
        seen.add(event(max(.001,deadline-time.monotonic())).get('event'))
    total_start=time.monotonic()
    def send(can_id,payload,request=None,cleanup=False):
        """Send one exact authorized frame and wait at most one second; never retry."""
        global number
        assert len(r['sent'])<344
        assert cleanup or time.monotonic()-total_start<57
        number+=1
        cmd=dict(id=number,op='send',can_id=can_id,data_hex=payload,extended=False,fd=False,remote=False,brs=False)
        r['sent'].append({'monotonic':time.monotonic(),'command':cmd})
        jcan.stdin.write(json.dumps(cmd)+'\n');jcan.stdin.flush()
        deadline=time.monotonic()+1;accepted=False;response=None
        while not (accepted and (request is None or response is not None)):
            v=event(max(.001,deadline-time.monotonic()))
            if v.get('id')==number:assert v.get('ok') is True;accepted=True
            if request and v.get('event')=='frame' and v['can_id']==0x581:
                data=bytes.fromhex(v['data_hex']);q=bytes.fromhex(payload)
                assert len(data)==8 and data[1:4]==q[1:4] and data[0] in (0x43,0x47,0x4b,0x4f),v
                size=4-((data[0]>>2)&3)
                response=int.from_bytes(data[4:4+size],'little')
                r['reads'].append(dict(object=request,value=response,monotonic=time.monotonic()))
        return response
    baseline=json.loads((root/'inventory_verification.json').read_text())['values']
    for q in m['preflight']:
        assert send('601',q['payload'],q['object'])==int(baseline[q['object']],16),q
        time.sleep(.1)
    started=True
    send('000','01 01')
    deadline=time.monotonic()+1
    while not tpdo:event(max(.001,deadline-time.monotonic()))
    print('MANUAL_READY: timer starts in 5 seconds',flush=True)
    countdown=time.monotonic()+5
    while time.monotonic()<countdown:
        try:event(min(.05,countdown-time.monotonic()))
        except queue.Empty:pass
    sample_start=time.monotonic();r['sample_start']=sample_start
    print('TIMER_STARTED 40 seconds',flush=True)
    for cycle in range(80):
        target=sample_start+cycle*.5
        while time.monotonic()<target:
            try:event(min(.05,target-time.monotonic()))
            except queue.Empty:pass
        assert tpdo and time.monotonic()-tpdo[-1]['monotonic']<.5,'stale TPDO'
        for q in m['sampling']['requests']:
            value=send('601',q['payload'],q['object'])
            if q['object']=='6041:00':assert all(((value>>s)&0x6f)==0x21 for s in [0,16])
        if cycle in [0,10,30,40,60]:print('PHASE '+str(cycle*.5),flush=True)
    while time.monotonic()<sample_start+40:
        try:event(min(.05,sample_start+40-time.monotonic()))
        except queue.Empty:pass
    cleanup_sent=True;send('000','80 01',cleanup=True)
    for q in m['final_reads']:
        value=send('601',q['payload'],q['object'])
        if q['object']=='6041:00':assert all(((value>>s)&0x6f)==0x21 for s in [0,16])
        else:assert value==0,(q,value)
    r['passed']=True
except Exception as exc:
    r['error']=repr(exc)
    print('STOP: '+repr(exc),flush=True)
finally:
    if started and not cleanup_sent:
        cleanup_sent=True
        try:send('000','80 01',cleanup=True)
        except Exception as exc:r['inhibit_cleanup_error']=repr(exc);print('OPERATOR_INTERVENTION_REQUIRED',flush=True)
    r['cleanup_sent']=cleanup_sent
    r['tpdo']=locals().get('tpdo',[])
    if jcan is not None:
        try:
            jcan.stdin.write('{"id":9999,"op":"shutdown"}\n');jcan.stdin.flush();jcan.stdin.close();jcan.wait(timeout=4)
            r['jcan_exit']=jcan.returncode;r['jcan_stderr']=jcan.stderr.read()
            if jcan.returncode or r['jcan_stderr']:r['passed']=False
        except Exception as exc:
            r['session_cleanup_error']=repr(exc);r['passed']=False;jcan.kill();jcan.wait()
    if remote is not None:
        try:
            remote.stdin.close();remote.wait(timeout=5)
            r['capture_exit']=remote.returncode;r['capture_stderr']=remote.stderr.read()
            if remote.returncode or r['capture_stderr']:r['passed']=False
        except Exception as exc:
            r['capture_cleanup_error']=repr(exc);r['passed']=False;remote.kill();remote.wait()
    (out/'result.json').write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps({k:v for k,v in r.items() if k not in ['sent','reads','tpdo','config_before']})+' requests='+str(len(r['sent']))+' tpdo='+str(len(r['tpdo'])),flush=True)
raise SystemExit(0 if r['passed'] else 1)
