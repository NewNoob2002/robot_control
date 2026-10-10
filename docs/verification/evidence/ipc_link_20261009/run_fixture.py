import json
import signal
import subprocess
import sys
import time
from pathlib import Path

root = Path(__file__).resolve().parent
rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 1
assert 1 <= rounds <= 3
fixture = node = None
with (root/'node.log').open('w') as log:
    try:
        node = subprocess.Popen([sys.executable, str(root/'robot_control_ipc_node.py'), '--ros-args',
            '-p', f'socket_path:={root}/control.sock', '-r', '__node:=ipc_verify', '-r', '__ns:=/verification'],
            stdout=log, stderr=subprocess.STDOUT)
        (root/'active-pids.json').write_text(json.dumps({'node':node.pid})+'\n')
        for attempt in range(rounds):
            fixture = subprocess.Popen([str(root/'robot_control_ipc_fixture'), str(root/'control.sock')],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            assert fixture.stdout.readline() == 'READY\n'
            print('FIXTURE_READY', attempt+1, flush=True)
            assert fixture.wait(timeout=10) == 0
            print('FIXTURE_EXIT', attempt+1, flush=True)
            if attempt+1 < rounds:
                time.sleep(2)
        time.sleep(1)
        node.send_signal(signal.SIGTERM)
        assert node.wait(timeout=3) == 0
    finally:
        for process in (fixture,node):
            if process is not None and process.poll() is None:
                process.kill()
                process.wait(timeout=3)
        (root/'postflight.json').write_text(json.dumps({'node_running':node is not None and node.poll() is None,
            'fixture_running':fixture is not None and fixture.poll() is None,
            'remaining_sockets':[str(p) for p in root.glob('*.sock')]},indent=2)+'\n')
