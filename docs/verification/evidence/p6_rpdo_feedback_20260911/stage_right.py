"""Stage hash-checked diagnostics and run only namespace-isolated vcan tests."""
import hashlib
import json
import shlex
import subprocess
from pathlib import Path
out = Path(__file__).resolve().parent
root = out.parents[3]
manifest = json.loads((out / "manifest.json").read_text())
base = manifest["base"]
assert base == "/tmp/robot-control-qualifications/rpdo-feedback-0e835fd8a9bc"
ssh = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "robot-dev"]
identity = subprocess.check_output([*ssh,"cat /etc/machine-id"],text=True,timeout=10).strip()
assert identity == "6923ab3301fb4a8d816759b04ec6bf0a"
for name,info in manifest["files"].items():
    if name not in ("authorization_right.json", "remote_motion_right.py"): continue
    source = root / info["source"]
    assert hashlib.sha256(source.read_bytes()).hexdigest() == info["sha256"]
    subprocess.run(["scp","-q","-o","BatchMode=yes","-o","ConnectTimeout=5",str(source),"robot-dev:"+base+"/"+name],check=True,timeout=20)
    actual = subprocess.check_output([*ssh,"sha256sum "+shlex.quote(base+"/"+name)],text=True,timeout=10).split()[0]
    assert actual == info["sha256"]
    print(name, actual, flush=True)
