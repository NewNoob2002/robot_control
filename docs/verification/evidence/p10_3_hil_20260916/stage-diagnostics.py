"""Stage the fixed no-motion TPDO2 trial; never execute it during staging."""
from pathlib import Path
import hashlib,subprocess,json,shlex
out=Path('docs/verification/evidence/p10_3_hil_20260916')
auth=json.loads((out/'diagnostics-authorization.json').read_text())
base=auth['destination']
binary=Path('out/build/cross/p103-diagnostics-reviewed-runtime/tools/zlac_qualification/robot-control-zlac-qualification')
assert hashlib.sha256(binary.read_bytes()).hexdigest()==auth['artifact_sha256']
ssh=['ssh','-o','BatchMode=yes','-o','ConnectTimeout=5','robot-dev']
subprocess.run(ssh+['test "$(cat /etc/machine-id)" = '+shlex.quote(auth['target_machine_id'])+' && mkdir '+shlex.quote(base)],check=True,timeout=10)
files={'robot-control-zlac-qualification':binary,'diagnostics-once.py':out/'diagnostics-once.py','authorization.json':out/'diagnostics-authorization.json'}
for name,source in files.items():
 subprocess.run(['scp','-q',str(source),'robot-dev:'+base+'/'+name],check=True,timeout=20)
verified=subprocess.check_output(ssh+['cd '+shlex.quote(base)+' && sha256sum '+ ' '.join(files)],text=True,timeout=10)
for line in verified.splitlines():
 digest,name=line.split()
 assert hashlib.sha256(files[name].read_bytes()).hexdigest()==digest
(out/'diagnostics-deployed-sha256.txt').write_text(verified)
r=subprocess.run(ssh+['timeout 5s '+shlex.quote(base+'/robot-control-zlac-qualification')+' --interface __no_device__ --runtime-diagnostics --duration-ms 2000'],capture_output=True,text=True,timeout=10)
(out/'diagnostics-target-no-device-smoke.log').write_text(r.stdout+r.stderr)
assert r.returncode==2,r.returncode
print('Deployed hashes matched; target invalid-argument/no-device smoke PASS; physical runner NOT executed.')
