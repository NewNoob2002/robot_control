"""One operator-triggered, bounded can0 down/up; no CAN transmissions or permission changes."""
import hashlib,json,os,select,subprocess,sys,time
from pathlib import Path
BASE = Path('/tmp/robot-control-qualifications/external-recovery-a8040dc1a783')
EXPECTED = 'a8040dc1a7837a481644a00ce48293aed7cf09ccb9ac4b31255f7cb71372d3e7'
assert os.geteuid() == 0, 'Run in the local terminal with sudo'
assert Path('/etc/machine-id').read_text().strip() == '6923ab3301fb4a8d816759b04ec6bf0a'
assert hashlib.sha256((BASE/'robot-control-zlac-qualification').read_bytes()).hexdigest() == EXPECTED
assert not (BASE/'interface_loss_manual_v4_once').exists(), 'Test already started; do not retry'
gate=BASE/'interface_manual_v4_gate'
gate.mkdir(exist_ok=False)
os.chmod(gate,0o755)
def save(name,value):
    """Publish one complete root-owned gate record atomically."""
    temporary=gate/(name+'.tmp')
    temporary.write_text(json.dumps(value,indent=2))
    os.chmod(temporary,0o644)
    temporary.replace(gate/name)
can=json.loads(subprocess.check_output(['/usr/sbin/ip','-j','-details','link','show','can0'],text=True))[0]
assert 'UP' in can['flags'] and can['linkinfo']['info_data']['bittiming']['bitrate'] == 500000
save('ready.json',{'uid':os.geteuid(),'pid':os.getpid(),'wall_time':time.time()})
print('READY: tell the agent this terminal is ready. Wait for the new trial. Press Enter ONCE when the RIGHT wheel moves.',flush=True)
assert select.select([sys.stdin],[],[],900)[0], 'No operator trigger within15minutes; no interface change'
assert sys.stdin.readline() == '\n', 'Enter only; no interface change'
arm=BASE/'interface_loss_manual_v4_once/armed.json'
end=time.monotonic()+2
while not arm.exists() and time.monotonic()<end: time.sleep(.01)
assert arm.exists(),'Application not armed; no interface change'
data=json.loads(arm.read_text())
assert data['elf_sha256']==EXPECTED and data['moving_tpdo_observed'] and time.time()-data['wall_time']<3
assert Path('/proc',str(data['pid']),'exe').resolve()==BASE/'robot-control-zlac-qualification'
event={'state':'armed','down_before_ns':time.time_ns()}
down=False
try:
    subprocess.run(['/usr/sbin/ip','link','set','dev','can0','down'],check=True,timeout=2)
    down=True
    event.update(state='down',down_after_ns=time.time_ns())
    save('event.json',event)
    time.sleep(3)
except Exception as error:
    event.update(state='failed',error=str(error))
    save('event.json',event)
    raise
finally:
    if down:
        event['up_before_ns']=time.time_ns()
        result=subprocess.run(['/usr/sbin/ip','link','set','dev','can0','up'],timeout=2)
        event.update(state='up' if result.returncode==0 else 'failed',up_after_ns=time.time_ns(),up_exit=result.returncode)
        save('event.json',event)
        assert result.returncode==0, 'Interface restore failed; use emergency stop'
print('DONE: can0 restored. Observe stopped wheels and no restart.',flush=True)
