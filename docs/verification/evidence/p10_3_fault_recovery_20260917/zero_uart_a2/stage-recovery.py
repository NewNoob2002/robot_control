"""Stage only the reviewed artifact and run device-free target smoke; never start HIL."""
from pathlib import Path
import hashlib
import json
import subprocess

base = Path(__file__).resolve().parent
root = base.parents[4]
auth = json.loads((base/'authorization.json').read_text())
remote = auth['staging']
build = root/'out/build/cross/p103-uart-partial-v1-runtime'
files = {
    'robot-control-hil': build/'tools/control_hil/robot-control-hil',
    'robot_control_control_cycle_tests': build/'robot_control_control_cycle_tests',
    'recovery-once.py': base/'recovery-once.py',
    'authorization.json': base/'authorization.json',
    'safety-preflight.json': base/'safety-preflight.json',
}
hashes = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name,path in files.items()}
assert hashes['robot-control-hil'] == auth['artifact_sha256']
preflight = '''from pathlib import Path
import platform
assert Path('/etc/machine-id').read_text().strip() == '6923ab3301fb4a8d816759b04ec6bf0a'
assert platform.machine() == 'aarch64'
for entry in Path('/proc').iterdir():
    if entry.name.isdigit():
        try:
            name=(entry/'exe').resolve(strict=True).name
            assert name not in ('robot-control-hil','robot-control-zlac-qualification','robot-control-canopen-commission','robot-control-sbus-observer','cansend','cangen'),name
        except (FileNotFoundError,PermissionError,ProcessLookupError): pass
Path(REMOTE).mkdir(exist_ok=False)
print('Target identity and absence of writers verified; new stage created')
'''.replace('REMOTE',repr(remote))
subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=5','robot-dev','python3 -'],input=preflight,text=True,check=True,timeout=12)
for name,path in files.items():
    subprocess.run(['scp','-q',str(path),'robot-dev:'+remote+'/'+name],check=True,timeout=15)
verify = '''from pathlib import Path
import hashlib,json,subprocess
base=Path(REMOTE)
hashes=HASHES
for name,expected in hashes.items(): assert hashlib.sha256((base/name).read_bytes()).hexdigest()==expected,name
assert not (base/'physical-once').exists()
subprocess.run([str(base/'robot-control-hil'),'--help'],check=True,timeout=5)
subprocess.run([str(base/'robot_control_control_cycle_tests')],check=True,timeout=10)
print(json.dumps({'verified':hashes,'physical_started':False,'marker_unconsumed':True,'smoke':'help and pure control-cycle passed'}))
'''.replace('REMOTE',repr(remote)).replace('HASHES',repr(hashes))
subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=5','robot-dev','python3 -'],input=verify,text=True,check=True,timeout=20)
