"""Read only bounded CAN function bytes from the on-disk kernel, never live memory."""
import base64
import json
import subprocess
from pathlib import Path

out = Path(__file__).resolve().parent
program = r'''
import base64,hashlib,json,struct
from pathlib import Path
symbols=[]
for line in Path('/boot/System.map-6.1.84').read_text().splitlines():
    fields=line.split()
    if len(fields)==3:
        symbols.append((int(fields[0],16),fields[1],fields[2]))
names={name:addr for addr,kind,name in symbols}
base=names['_text']
image=Path('/boot/vmlinuz-6.1.84')
result={'image_size':image.stat().st_size,'image_sha256':hashlib.sha256(image.read_bytes()).hexdigest(),
        'text_base':hex(base),'functions':{},'symbols':{}}
with image.open('rb') as f:
    header=f.read(64)
    assert header[56:60]==b'ARM\x64',header.hex()
    result['image_header_hex']=header.hex()
    for name in ('_text','_stext','__start_notes','__stop_notes','linux_banner'):
        if name in names:
            result['symbols'][name]=hex(names[name])
    f.seek(names['linux_banner']-base)
    result['image_banner']=f.read(512).split(b'\0')[0].decode()
    assert result['image_banner'].strip()==Path('/proc/version').read_text().strip()
    if '__start_notes' in names and '__stop_notes' in names:
        start,end=names['__start_notes'],names['__stop_notes']
        assert 0<end-start<65536
        f.seek(start-base)
        notes=f.read(end-start)
        live=Path('/sys/kernel/notes').read_bytes()
        result['notes_match_running_kernel']=notes==live
        result['running_notes_sha256']=hashlib.sha256(live).hexdigest()
        result['image_notes_sha256']=hashlib.sha256(notes).hexdigest()
    for index,(addr,kind,name) in enumerate(symbols):
        if (not name.startswith('rockchip_canfd_') and addr not in
                (0xffff8000089ee5d0, 0xffff8000089edc78, 0xffff8000089edc98)) or kind.lower()!='t':
            continue
        end=next(a for a,k,n in symbols[index+1:] if a>addr)
        assert 0<end-addr<16384
        f.seek(addr-base)
        data=f.read(end-addr)
        result['functions'][name]={'address':hex(addr),'size':len(data),
            'sha256':hashlib.sha256(data).hexdigest(),'base64':base64.b64encode(data).decode()}
print(json.dumps(result,indent=2))
'''
result = subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=5','robot-dev','python3 -'],
                        input=program,text=True,capture_output=True,timeout=30)
(out/'kernel_code_metadata.json').write_text(result.stdout)
(out/'kernel_code_metadata.stderr').write_text(result.stderr)
if result.returncode==0:
    metadata=json.loads(result.stdout)
    destination=out/'target_code'
    destination.mkdir(exist_ok=True)
    for name,info in metadata['functions'].items():
        (destination/(name+'.bin')).write_bytes(base64.b64decode(info['base64']))
    print('banner_match=True notes_match='+str(metadata.get('notes_match_running_kernel')))
print('kernel_code_exit='+str(result.returncode))
raise SystemExit(result.returncode)
