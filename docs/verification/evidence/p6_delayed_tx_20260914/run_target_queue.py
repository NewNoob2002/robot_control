"""Run a bounded virtual-only queue experiment on the matching RK3588 kernel."""
import shlex
import subprocess
from pathlib import Path

out = Path(__file__).resolve().parent
script = '/tmp/robot-control-qualifications/cable-repair-acc9f8828f65/reproduce_vxcan_queue.py'
inner = 'set -e; ip link add vxcan0 type vxcan peer name vxcan1; ip link set vxcan0 up; ip link set vxcan1 up; tc qdisc add dev vxcan0 root netem delay 400ms; export ROBOT_CONTROL_ISOLATED_QUEUE_TEST=1; python3 ' + script
command = 'timeout 10s unshare --user --map-root-user --net -- bash -c ' + shlex.quote(inner)
with (out / 'target_vxcan_queue.log').open('w') as log:
    result = subprocess.run(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5', 'robot-dev', command],
                            stdout=log, stderr=subprocess.STDOUT, timeout=20)
print('target_virtual_queue_exit=' + str(result.returncode))
raise SystemExit(result.returncode)
