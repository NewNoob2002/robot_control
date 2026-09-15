"""Stage the hash-locked v5 diagnostic package; never execute physical CAN stimulus."""
import hashlib
import json
import shlex
import subprocess
from pathlib import Path
out=Path(__file__).resolve().parent
root=out.parents[3]
manifest=json.loads((out/'manifest.json').read_text())
base=manifest['base']
assert base=='/tmp/robot-control-qualifications/interface-v5-'+manifest['elf_sha256'][:12]
ssh=['ssh','-o','BatchMode=yes','-o','ConnectTimeout=5','robot-dev']
assert subprocess.check_output([*ssh,'cat /etc/machine-id'],text=True,timeout=10).strip()=='6923ab3301fb4a8d816759b04ec6bf0a'
for info in manifest['files'].values():
    assert hashlib.sha256((root/info['source']).read_bytes()).hexdigest()==info['sha256']
subprocess.run([*ssh,'mkdir '+shlex.quote(base)],check=True,timeout=10)
for name,info in manifest['files'].items():
    subprocess.run(['scp','-q','-o','BatchMode=yes','-o','ConnectTimeout=5',str(root/info['source']),'robot-dev:'+base+'/'+name],check=True,timeout=20)
    assert subprocess.check_output([*ssh,'sha256sum '+shlex.quote(base+'/'+name)],text=True,timeout=10).split()[0]==info['sha256']
    print('verified '+name,flush=True)
(out/'stage_result.json').write_text(json.dumps({'base':base,'all_hashes_verified':True,'physical_executor_started':False},indent=2)+chr(10))
