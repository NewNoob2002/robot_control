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
assert base == "/tmp/robot-control-qualifications/sync-feedback-3885a78bda2d"
ssh = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "robot-dev"]
identity = subprocess.check_output([*ssh,"cat /etc/machine-id"],text=True,timeout=10).strip()
assert identity == "6923ab3301fb4a8d816759b04ec6bf0a"
subprocess.run([*ssh,"mkdir " + shlex.quote(base)],check=True,timeout=10)
for name,info in manifest["files"].items():
    source = root / info["source"]
    assert hashlib.sha256(source.read_bytes()).hexdigest() == info["sha256"]
    subprocess.run(["scp","-q","-o","BatchMode=yes","-o","ConnectTimeout=5",str(source),"robot-dev:"+base+"/"+name],check=True,timeout=20)
    actual = subprocess.check_output([*ssh,"sha256sum "+shlex.quote(base+"/"+name)],text=True,timeout=10).split()[0]
    assert actual == info["sha256"]
    print(name, actual, flush=True)
command = "bash " + shlex.quote(base+"/test_socketcan_vcan.sh") + " " + shlex.quote(base+"/review_qualification_vcan_tests")
result = subprocess.run([*ssh,command],capture_output=True,text=True,timeout=60)
(out/"target_vcan.log").write_text(result.stdout)
(out/"target_vcan.stderr").write_text(result.stderr)
assert result.returncode == 0 and "prohibited=0 failures=0" in result.stdout and "SKIP" not in result.stdout, result.stdout+result.stderr
print("Target isolated vcan passed",flush=True)
