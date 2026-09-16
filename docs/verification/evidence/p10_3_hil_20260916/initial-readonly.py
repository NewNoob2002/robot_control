"""Run a single bounded receive-only UART capture on the confirmed powered-off fixture."""
from pathlib import Path
import subprocess,json,shlex
out=Path('docs/verification/evidence/p10_3_hil_20260916')
base='/home/cat/.cache/robot-control/staging/p103-initial-60eb391-20260916'
code='''from pathlib import Path
import subprocess,hashlib,json
base=Path('/home/cat/.cache/robot-control/staging/p103-initial-60eb391-20260916')
assert Path('/etc/machine-id').read_text().strip()=='6923ab3301fb4a8d816759b04ec6bf0a'
assert hashlib.sha256((base/'robot-control-sbus-observer').read_bytes()).hexdigest()=='9c87ab4c4981d7ee3c8b5eef0aac7b8187d368a842946ae0260ab377284fdb54'
(base/'sbus-readonly-once.marker').open('x').close()
identity=subprocess.check_output(['udevadm','info','--query=property','--name=/dev/ttyACM0'],text=True)
assert 'ID_SERIAL_SHORT=586D017868'+chr(10) in identity
(base/'uart-identity.txt').write_text(identity)
holders=subprocess.run(['lsof','-nP','/dev/ttyACM0'],capture_output=True,text=True)
assert holders.returncode==1 and not holders.stdout and not holders.stderr
with (base/'sbus-readonly.log').open('wb') as log, (base/'sbus-readonly.stderr').open('wb') as err:
 result=subprocess.run([str(base/'robot-control-sbus-observer'),'--device','/dev/ttyACM0','--duration-ms','10000','--steering-axis','200,1000,1800,0','--throttle-axis','200,993,1800,0'],stdout=log,stderr=err,timeout=15)
(base/'sbus-readonly-exit.txt').write_text(str(result.returncode)+chr(10))
holders=subprocess.run(['lsof','-nP','/dev/ttyACM0'],capture_output=True,text=True)
(base/'uart-holders-after.txt').write_text(hoders if False else holders.stdout+holders.stderr)
print(json.dumps({'observer_exit':result.returncode,'holders_after_exit':holders.returncode}))
assert result.returncode==0 and holders.returncode==1
'''
cmd=['ssh','-o','BatchMode=yes','-o','ConnectTimeout=5','robot-dev','python3 -c '+shlex.quote(code)]
r=subprocess.run(cmd,capture_output=True,text=True,timeout=25)
(out/'sbus-readonly-runner.log').write_text(r.stdout+r.stderr)
for name in ('uart-identity.txt','sbus-readonly.log','sbus-readonly.stderr','sbus-readonly-exit.txt','uart-holders-after.txt'):
 subprocess.run(['scp','-q','robot-dev:'+base+'/'+name,str(out/name)],check=True,timeout=10)
print(r.stdout+r.stderr)
assert r.returncode==0
