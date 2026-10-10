import base64
import json
import os
from pathlib import Path
import subprocess
import sys

root=Path.cwd()
out=root/'out/verification/ipc-network-diagnosis-20261009'
mode=sys.argv[1]
assert mode in ('default','udp')
rounds=2
config=json.loads((out/'PREFLIGHT.json').read_text())
stage=config['destination']
image=next(line.split('=',1)[1] for line in (root/'docker/ros2/image.lock').read_text().splitlines() if line.startswith('upstream_image='))
for suffix in ('.pcap','.capture.json'):
    assert not (out/(mode+suffix)).exists()
trace_name=f'robot-control-nettrace-{mode}'
assert subprocess.run(['docker','container','inspect',trace_name],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode!=0
capture_cmd=['docker','run','--rm','--pull=never','--name',trace_name,'--network','host',
    '--read-only','--user','0:0','--cap-drop','ALL','--cap-add','NET_RAW','--security-opt','no-new-privileges',
    '--mount',f'type=bind,source={out},target=/output,readonly',image,'python3','-u','/output/capture_rtps.py',
    'wlan0',config['target_address'],config['host_address'],f'/output/{mode}']
capture=subprocess.Popen(capture_cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
observer=None
try:
    line=capture.stdout.readline()
    assert line.strip()=='CAPTURE_READY',line
    command=['docker','run','--rm','--pull=never','--network','host',
        '--user',f'{os.getuid()}:{os.getgid()}','--read-only','--tmpfs','/tmp:rw,nosuid,nodev',
        '--cap-drop','ALL','--security-opt','no-new-privileges','--env','ROS_HOME=/tmp/ros2',
        '--env','ROS_LOG_DIR=/tmp/ros2/log','--env','ROS_LOCALHOST_ONLY=0','--env','ROS_DOMAIN_ID=186',
        '--env','RMW_IMPLEMENTATION=rmw_fastrtps_cpp','--env','PYTHONDONTWRITEBYTECODE=1',
        '--mount',f'type=bind,source={root},target=/workspace','--workdir','/workspace']
    env=''
    if mode!='default':
        command+=['--env',f'FASTRTPS_DEFAULT_PROFILES_FILE=/workspace/out/verification/ipc-network-diagnosis-20261009/host-{mode}.xml']
        env=f' FASTRTPS_DEFAULT_PROFILES_FILE={stage}/board-udp-current.xml'
    command+=[image,'timeout','40','python3','scripts/test/test_ros2_ipc_remote_status.py',
        '--duration-sec','26','--min-instances',str(rounds),
        '--min-stale-events','1',
        '--output',f'out/verification/ipc-network-diagnosis-20261009/{mode}-dds.json']
    observer=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    ready=observer.stdout.readline()
    assert ready.strip()=='READY',ready
    remote=("env -u PYTHONPATH -u LD_LIBRARY_PATH -u AMENT_PREFIX_PATH -u COLCON_PREFIX_PATH bash -c "
        f"'source /opt/ros/humble/setup.bash; export ROS_DOMAIN_ID=186 ROS_LOCALHOST_ONLY=0 RMW_IMPLEMENTATION=rmw_fastrtps_cpp{env}; "
        f"timeout 38 python3 {stage}/run_fixture.py {rounds}'")
    with (out/f'{mode}-board.log').open('w') as log:
        board=subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8',config['target_alias'],remote],
            stdout=log,stderr=subprocess.STDOUT,timeout=42)
    text=ready+observer.communicate(timeout=42)[0]
    (out/f'{mode}-host.log').write_text(text)
    print(text,flush=True)
    capture_result=json.loads(capture.communicate(timeout=45)[0])
    (out/(mode+'.pcap')).write_bytes(base64.b64decode(capture_result.pop('pcap_base64'),validate=True))
    (out/(mode+'.capture.json')).write_text(json.dumps(capture_result,indent=2)+'\n')
    capture_text=line+f"CAPTURE_COMPLETE {sum(capture_result['counts'].values())}\n"
    (out/f'{mode}-capture.log').write_text(capture_text)
    print(capture_text,flush=True)
    for name in ('postflight.json','node.log'):
        subprocess.run(['scp','-q','-o','BatchMode=yes',f"{config['target_alias']}:{stage}/{name}",str(out/f'{mode}-{name}')],check=True,timeout=10)
    (out/f'{mode}-execution.json').write_text(json.dumps({'capture_command':capture_cmd,
        'capture_returncode':capture.returncode,'host_command':command,'host_returncode':observer.returncode,
        'board_command':remote,'board_returncode':board.returncode},indent=2)+'\n')
    print('RETURN_CODES',board.returncode,observer.returncode,capture.returncode,flush=True)
finally:
    if observer is not None and observer.poll() is None:
        observer.terminate()
        observer.wait(timeout=3)
    if capture.poll() is None:
        subprocess.run(['docker','stop','--time','2',trace_name],timeout=5)
        capture.wait(timeout=5)
