"""Stage separate cable-loss authorization and wrapper while preserving the verified existing ELF."""
import hashlib
import json
import shlex
import subprocess
from pathlib import Path
out=Path(__file__).resolve().parent
manifest=json.loads((out/'manifest.json').read_text())
base=manifest['base']
assert base=='/tmp/robot-control-qualifications/interface-v5-2304c1c9892d'
ssh=['ssh','-o','BatchMode=yes','-o','ConnectTimeout=5','robot-dev']
assert subprocess.check_output([*ssh,'cat /etc/machine-id'],text=True,timeout=10).strip()=='6923ab3301fb4a8d816759b04ec6bf0a'
assert subprocess.check_output([*ssh,'sha256sum '+shlex.quote(base+'/robot-control-zlac-qualification')],text=True,timeout=10).split()[0]==manifest['elf_sha256']
subprocess.run([*ssh,'test ! -e '+shlex.quote(base+'/cable_loss_once')],check=True,timeout=10)
for name,remote in (('authorization.json','authorization_cable_loss_once.json'),('remote_trial.py','remote_cable_loss_once.py')):
    source=out/name
    assert hashlib.sha256(source.read_bytes()).hexdigest()==manifest['files'][name]
    subprocess.run([*ssh,'test ! -e '+shlex.quote(base+'/'+remote)],check=True,timeout=10)
    subprocess.run(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=5',str(source),'robot-dev:'+base+'/'+remote],check=True,timeout=20)
    assert subprocess.check_output([*ssh,'sha256sum '+shlex.quote(base+'/'+remote)],text=True,timeout=10).split()[0]==manifest['files'][name]
(out/'stage_result.json').write_text(json.dumps({'base':base,'hashes_verified':True,'physical_executor_started':False},indent=2)+chr(10))
print('Cable-loss wrapper staged and hash verified; no physical executor run')
