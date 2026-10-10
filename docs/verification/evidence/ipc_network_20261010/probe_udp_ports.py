import base64
import json
import select
import shlex
import socket
import subprocess
import time
from pathlib import Path

out=Path('out/verification/ipc-network-diagnosis-20261009').resolve()
config=json.loads((out/'PREFLIGHT.json').read_text())
command=json.loads((out/'default-execution.json').read_text())['capture_command']
command[command.index('--name')+1]='robot-control-nettrace-port-probe'
command[-1]='/output/port-probe'
command.append('10')
capture=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
board=None
sockets=[]
try:
    assert capture.stdout.readline().strip()=='CAPTURE_READY'
    for port in (54710,54711):
        peer=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        peer.bind((config['host_address'],port))
        peer.setblocking(False)
        sockets.append(peer)
    code='''import socket,sys,time
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as peer:
    peer.bind((sys.argv[2],54820))
    peer.settimeout(4)
    print('READY',flush=True)
    time.sleep(.3)
    for _ in range(2):
        for port in (54710,54711):
            peer.sendto(b'RC_UDP_DIAG SPONTANEOUS',(sys.argv[1],port))
    data,source=peer.recvfrom(256)
    assert data==b'RC_UDP_DIAG PING' and source==(sys.argv[1],54710)
    peer.sendto(b'RC_UDP_DIAG PONG',source)
    print('PONG_SENT',flush=True)
'''
    args=['timeout','8','/usr/bin/python3','-u','-c',code,config['host_address'],config['target_address']]
    remote=' '.join(shlex.quote(value) for value in args)
    board=subprocess.Popen(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8',config['target_alias'],remote],
        stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    assert board.stdout.readline().strip()=='READY'
    spontaneous=[]
    deadline=time.monotonic()+2
    while time.monotonic()<deadline:
        for peer in select.select(sockets,[],[],.1)[0]:
            data,source=peer.recvfrom(256)
            spontaneous.append({'port':peer.getsockname()[1],'data':data.decode(),'source':source})
    sockets[0].sendto(b'RC_UDP_DIAG PING',(config['target_address'],54820))
    assert select.select([sockets[0]],[],[],3)[0], 'related UDP reply did not arrive'
    data,source=sockets[0].recvfrom(256)
    assert data==b'RC_UDP_DIAG PONG' and source==(config['target_address'],54820)
    board_text=board.communicate(timeout=6)[0]
    assert board.returncode==0
    result=json.loads(capture.communicate(timeout=15)[0])
    (out/'port-probe.pcap').write_bytes(base64.b64decode(result.pop('pcap_base64'),validate=True))
    (out/'port-probe.capture.json').write_text(json.dumps(result,indent=2)+'\n')
    result={'spontaneous_delivered':spontaneous,'related_reply_delivered':True,'source':source,
            'capture_returncode':capture.returncode,'board_returncode':board.returncode,
            'host_sockets_closed':True,'capture_command':command,'board_command':remote}
    (out/'port-probe.json').write_text(json.dumps(result,indent=2)+'\n')
    (out/'port-probe-board.log').write_text(board_text)
    print('UDP_PORT_PROBE',json.dumps({'spontaneous_delivered':len(spontaneous),'related_reply_delivered':True}),flush=True)
finally:
    for peer in sockets:
        peer.close()
    if board is not None and board.poll() is None:
        board.terminate()
        board.wait(timeout=3)
    if capture.poll() is None:
        subprocess.run(['docker','stop','--time','2','robot-control-nettrace-port-probe'],timeout=5)
        capture.wait(timeout=5)
