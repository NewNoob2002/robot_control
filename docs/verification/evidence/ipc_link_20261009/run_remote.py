import json
import os
from pathlib import Path
import subprocess
import sys
import time

root=Path.cwd()
out=root/'out/verification/ipc-verification-20261009'
mode=sys.argv[1]
rounds=int(sys.argv[2]) if len(sys.argv)>2 else 1
assert mode in ('default','udp','tcp','tcp-proxy','tcp-proxy-wan','tcp-container') and rounds in (1,2,3)
stage='/home/cat/robot-control-tests/ipc-link-verification-20261009'
image=next(line.split('=',1)[1] for line in (root/'docker/ros2/image.lock').read_text().splitlines() if line.startswith('upstream_image='))
container='robot-control-ipc-link-20261009'
if mode=='tcp-container':
    assert subprocess.run(['docker','container','inspect',container],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode!=0
command=['docker','run','--rm','--pull=never','--network','bridge' if mode=='tcp-container' else 'host','--user',f'{os.getuid()}:{os.getgid()}',
    '--read-only','--tmpfs','/tmp:rw,nosuid,nodev','--cap-drop','ALL','--security-opt','no-new-privileges',
    '--env','ROS_HOME=/tmp/ros2','--env','ROS_LOG_DIR=/tmp/ros2/log','--env','ROS_LOCALHOST_ONLY=0',
    '--env','ROS_DOMAIN_ID=186','--env','RMW_IMPLEMENTATION=rmw_fastrtps_cpp','--env','PYTHONDONTWRITEBYTECODE=1',
    '--mount',f'type=bind,source={root},target=/workspace','--workdir','/workspace']
profile=''
if mode!='default':
    transport='tcp' if mode=='tcp-container' else mode
    command+=['--env',f'FASTRTPS_DEFAULT_PROFILES_FILE=/workspace/out/verification/ipc-verification-20261009/host-{transport}.xml']
    profile=f" FASTRTPS_DEFAULT_PROFILES_FILE={stage}/board-{'tcp' if mode in ('tcp-proxy','tcp-container') else mode}.xml"
if mode=='tcp-container':
    command+=['--name',container]
command += [image,'timeout','50','python3','scripts/test/test_ros2_ipc_remote_status.py',
    '--duration-sec',str(36 if mode=='tcp-container' else 14 if rounds==1 else 32 if rounds==3 else 26),'--min-instances',str(rounds),
    '--min-stale-events','0' if rounds==1 else '1',
    '--output',f'out/verification/ipc-verification-20261009/{mode}-dds.json']
if mode=='tcp-container':
    command+=['--network-events','out/verification/ipc-verification-20261009/tcp-container-network.json']
observer=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
remote_command=("env -u PYTHONPATH -u LD_LIBRARY_PATH -u AMENT_PREFIX_PATH -u COLCON_PREFIX_PATH bash -c "
    f"'source /opt/ros/humble/setup.bash; export ROS_DOMAIN_ID=186 ROS_LOCALHOST_ONLY=0 RMW_IMPLEMENTATION=rmw_fastrtps_cpp{profile}; "
    f"timeout 40 python3 {stage}/run_fixture.py {rounds}'")
try:
    ready=observer.stdout.readline()
    print('Observer:',ready.strip(),flush=True)
    assert ready.strip()=='READY',ready
    with (out/f'{mode}-board.log').open('w') as log:
        remote=subprocess.Popen(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8','robot-dev',remote_command],
            stdout=log,stderr=subprocess.STDOUT)
        if mode=='tcp-container':
            deadline=time.monotonic()+24
            interrupted=None
            while time.monotonic()<deadline:
                capture=out/'tcp-container-dds.jsonl'
                rows=[json.loads(line) for line in capture.read_text().splitlines() if line] if capture.exists() else []
                samples=[row for row in rows if row['status'].get('connected') and 'cycles' in row['status']]
                instances={row['status']['server_started_ns'] for row in samples}
                if len(instances)>=2:
                    latest=samples[-1]
                    instance=latest['status']['server_started_ns']
                    if (sum(row['status']['server_started_ns']==instance for row in samples)>=20
                            and time.monotonic()-latest['host_received_monotonic']<.1):
                        interrupted=instance
                        break
                time.sleep(.05)
            assert interrupted is not None, 'no live second-instance stream for network injection'
            subprocess.run(['docker','network','disconnect','bridge',container],check=True,timeout=5)
            down=time.monotonic()
            time.sleep(2)
            subprocess.run(['docker','network','connect','bridge',container],check=True,timeout=5)
            up=time.monotonic()
            (out/'tcp-container-network.json').write_text(json.dumps({'disconnected_monotonic':down,
                'reconnected_monotonic':up,'interrupted_server_started_ns':interrupted,
                'container':container,'network':'bridge','host_or_board_links_changed':False},indent=2)+'\n')
        remote.wait(timeout=45)
    text=ready+observer.communicate(timeout=50)[0]
    (out/f'{mode}-host.log').write_text(text)
    print(text,flush=True)
    result=dict(host_returncode=observer.returncode, board_returncode=remote.returncode,
                host_command=command, board_command=remote_command)
    (out/f'{mode}-execution.json').write_text(json.dumps(result,indent=2)+'\n')
    for name in ('postflight.json','node.log'):
        subprocess.run(['scp','-q','-o','BatchMode=yes',f'robot-dev:{stage}/{name}',str(out/f'{mode}-{name}')],check=True,timeout=10)
    print('Return codes:',remote.returncode,observer.returncode,flush=True)
finally:
    if observer.poll() is None:
        if mode=='tcp-container':
            subprocess.run(['docker','stop','--time','2',container],timeout=5)
        observer.terminate()
        observer.wait(timeout=3)
