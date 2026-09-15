"""Read kernel build metadata and netlink CAN capabilities without changing them."""
import subprocess
from pathlib import Path

out = Path(__file__).resolve().parent
program = r'''
import json,socket,struct,subprocess
from pathlib import Path
headers=Path('/usr/src/linux-headers-6.1.84')
paths=['.scmversion','include/config/kernel.release','include/generated/compile.h',
       'include/generated/utsrelease.h','include/uapi/linux/can/netlink.h']
result={'metadata':{}}
for name in paths:
    p=headers/name
    result['metadata'][name]=p.read_text() if p.is_file() else None
config=headers/'.config'
if config.is_file():
    result['selected_config']=[line for line in config.read_text().splitlines() if any(key in line for key in
        ('CONFIG_CAN','CONFIG_NET_SCH_NETEM','CONFIG_LOCALVERSION','CONFIG_BUILD_SALT','CONFIG_IKCONFIG','CONFIG_DEBUG_INFO_BTF'))]
result['boot_files']=[p.name for p in Path('/boot').iterdir() if not p.name.startswith('.')]
result['kernel_packages']=subprocess.run(['dpkg-query','-W','linux-image*','linux-headers*'],capture_output=True,text=True).stdout
def attrs(blob):
    values={}
    offset=0
    while offset+4<=len(blob):
        length,kind=struct.unpack_from('=HH',blob,offset)
        assert length>=4 and offset+length<=len(blob)
        values[kind & 0x3fff]=blob[offset+4:offset+length]
        offset+=(length+3)&~3
    return values
s=socket.socket(socket.AF_NETLINK,socket.SOCK_RAW,socket.NETLINK_ROUTE)
s.settimeout(3)
s.bind((0,0))
index=socket.if_nametoindex('can0')
payload=struct.pack('=BBHiII',socket.AF_UNSPEC,0,0,index,0,0)
request=struct.pack('=IHHII',16+len(payload),18,1,1,0)+payload
s.send(request)
packet=s.recv(65536)
length,kind,flags,seq,pid=struct.unpack_from('=IHHII',packet)
assert kind==16 and seq==1,(kind,packet.hex())
outer=attrs(packet[32:length])
linkinfo=attrs(outer[18])
assert linkinfo[1].rstrip(b'\x00')==b'can'
can=attrs(linkinfo[2])
result['can_netlink_attrs']={str(k):v.hex() for k,v in can.items()}
result['can_ctrlmode_mask_flags']=struct.unpack('=II',can[5])
s.close()
print(json.dumps(result,indent=2))
'''
result = subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=5','robot-dev','python3 -'],
                        input=program,text=True,capture_output=True,timeout=20)
(out/'target_provenance.json').write_text(result.stdout)
(out/'target_provenance.stderr').write_text(result.stderr)
print('metadata_exit='+str(result.returncode))
raise SystemExit(result.returncode)
