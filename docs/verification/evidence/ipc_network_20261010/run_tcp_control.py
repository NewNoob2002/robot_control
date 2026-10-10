import json
import os
from pathlib import Path
import subprocess

root=Path.cwd()
out=root/'out/verification/ipc-network-diagnosis-20261009'
config=json.loads((out/'PREFLIGHT.json').read_text())
stage=config['destination']
image=next(line.split('=',1)[1] for line in (root/'docker/ros2/image.lock').read_text().splitlines() if line.startswith('upstream_image='))
command=['docker','run','--rm','--pull=never','--network','host','--user',f'{os.getuid()}:{os.getgid()}',
    '--read-only','--tmpfs','/tmp:rw,nosuid,nodev','--cap-drop','ALL','--security-opt','no-new-privileges',
    '--env','ROS_HOME=/tmp/ros2','--env','ROS_LOG_DIR=/tmp/ros2/log','--env','ROS_LOCALHOST_ONLY=0',
    '--env','ROS_DOMAIN_ID=186','--env','RMW_IMPLEMENTATION=rmw_fastrtps_cpp','--env','PYTHONDONTWRITEBYTECODE=1',
    '--env','FASTRTPS_DEFAULT_PROFILES_FILE=/workspace/docs/verification/evidence/ipc_link_20261009/host-tcp.xml',
    '--mount',f'type=bind,source={root},target=/workspace','--workdir','/workspace',image,
    'timeout','40','python3','scripts/test/test_ros2_ipc_remote_status.py','--duration-sec','26',
    '--min-instances','2','--min-stale-events','1','--output',
    'out/verification/ipc-network-diagnosis-20261009/tcp-dds.json']
observer=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
try:
    ready=observer.stdout.readline()
    assert ready.strip()=='READY',ready
    remote=("env -u PYTHONPATH -u LD_LIBRARY_PATH -u AMENT_PREFIX_PATH -u COLCON_PREFIX_PATH bash -c "
        f"'source /opt/ros/humble/setup.bash; export ROS_DOMAIN_ID=186 ROS_LOCALHOST_ONLY=0 RMW_IMPLEMENTATION=rmw_fastrtps_cpp "
        f"FASTRTPS_DEFAULT_PROFILES_FILE={stage}/board-tcp.xml; timeout 38 python3 {stage}/run_fixture.py 2'")
    with (out/'tcp-board.log').open('w') as log:
        board=subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8',config['target_alias'],remote],
            stdout=log,stderr=subprocess.STDOUT,timeout=42)
    text=ready+observer.communicate(timeout=42)[0]
    (out/'tcp-host.log').write_text(text)
    print(text,flush=True)
    for name in ('postflight.json','node.log'):
        subprocess.run(['scp','-q','-o','BatchMode=yes',f"{config['target_alias']}:{stage}/{name}",str(out/f'tcp-{name}')],check=True,timeout=10)
    (out/'tcp-execution.json').write_text(json.dumps({'host_command':command,'host_returncode':observer.returncode,
        'board_command':remote,'board_returncode':board.returncode},indent=2)+'\n')
    assert board.returncode==observer.returncode==0
finally:
    if observer.poll() is None:
        observer.terminate()
        observer.wait(timeout=3)
