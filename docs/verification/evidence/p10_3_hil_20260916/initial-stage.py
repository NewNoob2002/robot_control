"""Stage reviewed no-CAN binaries into one new nonproduction target directory."""
from pathlib import Path
import subprocess,hashlib,json,shlex
root=Path('docs/verification/evidence/p10_3_hil_20260916')
base='/home/cat/.cache/robot-control/staging/p103-initial-60eb391-20260916'
ssh=['ssh','-o','BatchMode=yes','-o','ConnectTimeout=5','robot-dev']
check='test "$(cat /etc/machine-id)" = 6923ab3301fb4a8d816759b04ec6bf0a && mkdir '+shlex.quote(base)
subprocess.run(ssh+[check],check=True,timeout=10)
manifest=json.loads((root/'initial-artifacts.json').read_text())
build=Path('out/build/cross/p102-reviewed-runtime')
for name in manifest:
 p=build/name if name.endswith('_tests') else build/'tools/sbus_observer'/name
 assert hashlib.sha256(p.read_bytes()).hexdigest()==manifest[name]
 subprocess.run(['scp','-q',str(p),'robot-dev:'+base+'/'+name],check=True,timeout=20)
result=subprocess.run(ssh+['cd '+shlex.quote(base)+' && sha256sum robot_control_control_cycle_tests robot-control-sbus-observer'],capture_output=True,text=True,timeout=10,check=True)
(root/'initial-deployed-sha256.txt').write_text(result.stdout)
for line in result.stdout.splitlines():
 digest,name=line.split()
 assert manifest[name]==digest
result=subprocess.run(ssh+['timeout 10s '+shlex.quote(base+'/robot_control_control_cycle_tests')],capture_output=True,text=True,timeout=15)
(root/'initial-target-smoke.log').write_text(result.stdout+result.stderr)
(root/'initial-target-smoke-exit.txt').write_text(str(result.returncode)+chr(10))
assert result.returncode==0,result.stdout+result.stderr
print('Target no-device smoke PASS; staged binaries hash matched.')
