"""Run the final-source verification build without network or target hardware access."""
import os
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[3]
evidence = root / 'docs/verification/evidence/p6_nmt_stop_repair_20260911'
command = ['docker', 'run', '--rm', '--user', str(os.getuid()) + ':' + str(os.getgid()),
           '--read-only', '--network', 'none', '--cap-drop', 'ALL',
           '--security-opt', 'no-new-privileges', '--tmpfs', '/tmp:rw,nosuid,nodev',
           '--env', 'CCACHE_DISABLE=1', '--env', 'ROBOT_CONTROL_SYSROOT=/opt/robot-control/sysroot',
           '--mount', 'type=bind,source=' + str(root / 'out/build-inputs/nmt-stop-repair-final') + ',target=/workspace,readonly',
           '--mount', 'type=bind,source=' + str(root / 'out') + ',target=/workspace/out',
           '--mount', 'type=bind,source=' + str(root / 'sysroots/rk3588-ubuntu2204') + ',target=/opt/robot-control/sysroot,readonly',
           '--workdir', '/workspace',
           'sha256:f2198e31e27c084bc2deff761e124fa9d7ce580a8d986c7885fd62bb1701e7dd',
           'python3', '/workspace/out/hil/nmt-stop-repair/cross.py']
with (evidence / 'cross_final.log').open('w') as log:
    result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
print('cross_exit=' + str(result.returncode), flush=True)
raise SystemExit(result.returncode)
