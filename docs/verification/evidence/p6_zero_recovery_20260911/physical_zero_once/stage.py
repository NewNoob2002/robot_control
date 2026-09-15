"""Stage the reviewed zero-only wrapper without executing the drive application."""
import hashlib,json,subprocess
from pathlib import Path
out=Path(__file__).resolve().parent
m=json.loads((out/'runner_manifest.json').read_text())
base=m['base']
assert base=='/tmp/robot-control-qualifications/zero-recovery-31d2d0848e93'
ssh=['ssh','-o','BatchMode=yes','-o','ConnectTimeout=5','robot-dev']
assert subprocess.check_output([*ssh,'cat /etc/machine-id'],text=True,timeout=10).strip()=='6923ab3301fb4a8d816759b04ec6bf0a'
assert subprocess.check_output([*ssh,'sha256sum '+base+'/robot-control-zlac-qualification'],text=True,timeout=10).split()[0]=='31d2d0848e9330f6f910e642d47bccb6552ca112cefa36214defa646c524b7e3'
for name,remote in [('authorization.json','authorization_physical_zero_once.json'),('remote_trial.py','remote_physical_zero_once.py')]:
    source=out/name
    assert hashlib.sha256(source.read_bytes()).hexdigest()==m['files'][name]
    subprocess.run([*ssh,'test ! -e '+base+'/'+remote],check=True,timeout=10)
    subprocess.run(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=5',str(source),'robot-dev:'+base+'/'+remote],check=True,timeout=20)
    assert subprocess.check_output([*ssh,'sha256sum '+base+'/'+remote],text=True,timeout=10).split()[0]==m['files'][name]
print('Zero-only wrapper staged and hash verified; no physical executor run')
