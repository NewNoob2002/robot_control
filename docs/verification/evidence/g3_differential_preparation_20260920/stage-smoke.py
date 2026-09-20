"""Stage one verified development artifact and run only device-free target checks."""
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

BASE = Path(__file__).resolve().parent
ROOT = next(p for p in BASE.parents if (p/'CMakePresets.json').exists())
REMOTE = '/home/cat/.cache/robot-control/staging/g3-differential-51f94c1f-20260920'
SHA = '51f94c1f51945f3d28fa945d58f14cb15ad4ec7a4cab353331a2f39e7ca16451'
MACHINE = '6923ab3301fb4a8d816759b04ec6bf0a'
binary = ROOT/'out/build/cross/g3-differential-v1-runtime/tools/control_hil/robot-control-hil'
assert hashlib.sha256(binary.read_bytes()).hexdigest()==SHA
for name in ('debug','sanitizer'):
    tests = ET.parse(BASE/(name+'.xml')).getroot()
    assert int(tests.attrib['tests'])==53
    assert all(int(tests.attrib.get(key,'0'))==0 for key in ('failures','errors','disabled'))
    assert not tests.findall('.//skipped')
ssh = ['ssh','-o','BatchMode=yes','-o','ConnectTimeout=5','robot-dev']
identity = subprocess.check_output(ssh+['cat /etc/machine-id'],text=True,timeout=8).strip()
assert identity==MACHINE
subprocess.run(ssh+['mkdir '+REMOTE],check=True,timeout=8)
subprocess.run(['scp','-q',str(binary),'robot-dev:'+REMOTE+'/robot-control-hil'],check=True,timeout=15)
script = '''
import hashlib,json,subprocess
from pathlib import Path
remote=Path(REMOTE_VALUE)
binary=remote/'robot-control-hil'
assert Path('/etc/machine-id').read_text().strip()==MACHINE_VALUE
assert hashlib.sha256(binary.read_bytes()).hexdigest()==SHA_VALUE
device='/dev/robot-control-differential-smoke-missing'
assert not Path(device).exists() and not Path(device).is_symlink()
base=['--interface','can0','--device',device,'--duration-ms','2000']
checks=[]
for args,expected in ((['--help'],0),
                      (base+['--zero-only','--right-sign','-1'],1),
                      (base+['--differential-left','--right-sign','-1'],1),
                      (base+['--differential-right'],2),
                      (base+['--differential-left','--right-sign','-1','--motion-window-ms','5000'],2)):
    result=subprocess.run([str(binary),*args],capture_output=True,text=True,timeout=4)
    checks.append(dict(args=args,exit=result.returncode,stdout=result.stdout,stderr=result.stderr))
    assert result.returncode==expected,checks[-1]
    if expected==1: assert 'phase=uart ok=0 operation=open' in result.stderr and 'errno=2' in result.stderr
    if expected==0: assert 'positive equal-wheel approach' in result.stdout
print(json.dumps(dict(machine_id=MACHINE_VALUE,sha256=SHA_VALUE,remote=str(remote),checks=checks,
                     device_free=True,hardware_run=False)))
'''.replace('REMOTE_VALUE',repr(REMOTE)).replace('MACHINE_VALUE',repr(MACHINE)).replace('SHA_VALUE',repr(SHA))
output = subprocess.check_output(ssh+['python3 -B -'],input=script,text=True,timeout=30)
report = json.loads(output)
(BASE/'target-smoke.json').write_text(json.dumps(report,indent=2)+chr(10))
print('PASS: target identity/hash and5 device-free checks; no hardware test')
