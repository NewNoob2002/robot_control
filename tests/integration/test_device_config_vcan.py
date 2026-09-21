#!/usr/bin/env python3
"""Verify reviewed device parameters and fixed PDO provisioning on virtual CAN only."""
import os
from pathlib import Path
import select
import socket
import struct
import subprocess
import tempfile
import time
from test_app_vcan import device_baseline

# Independent expected OD values: no production schemas or encoders are imported.
PARAMETERS = {(0x2026,5): ('speed_resolution',10), (0x6060,0): ('drive_mode',3),
              (0x200F,0): ('drive_sync',1), (0x605A,0): ('drive_quick_stop',5),
              (0x2008,0): ('drive_max_rpm',90), (0x6083,1): ('drive_accel_left_ms',800),
              (0x6083,2): ('drive_accel_right_ms',900), (0x6084,1): ('drive_decel_left_ms',1000),
              (0x6084,2): ('drive_decel_right_ms',1100), (0x6085,1): ('drive_quick_decel_left_ms',50),
              (0x6085,2): ('drive_quick_decel_right_ms',60), (0x2026,3): ('drive_io_stop',258),
              (0x2007,0): ('drive_start_lock',1), (0x2026,4): ('drive_parking',1),
              (0x2026,7): ('drive_direction',1)}


def width(key):
    """Independent widths from the reviewed vendor OD, including full U32 timing writes."""
    index, sub = key
    if index in (0x6060,0x6061) or (index in (0x1400,0x1800,0x1801) and sub == 2) or (index in (0x1600,0x1A00,0x1A01) and sub == 0):
        return 1
    if index in (0x2007,0x2008,0x2026,0x6040,0x605A,0x1017,0x2000,0x200F) or (index in (0x1400,0x1800,0x1801) and sub == 5):
        return 2
    return 4


def trial(case):
    """Audit writes, preservation, PDO order, save gating and terminal outcome against a peer."""
    values = device_baseline(1)
    pdo_expected = {key: value for key, value in values.items() if 0x1400 <= key[0] <= 0x1A01}
    desired = {key: value for key, (_,value) in PARAMETERS.items()}
    if case in ('unchanged','keep'):
        values.update(desired)
    if case == 'keep':
        # Only the four mandatory profile fields are selected; everything else must stay untouched.
        for key in list(desired):
            if key not in ((0x2026,5),(0x6060,0),(0x200F,0),(0x605A,0)):
                del desired[key]
                values[key] = 42
    if case == 'pdo':
        values.update({(0x6060,0): 0, (0x6061,0): 0, (0x200F,0): 0, (0x605A,0): 7})
        values.update({(0x1600,0): 1, (0x1A00,0): 0, (0x1A00,1): 0, (0x1A00,2): 0})
    if case == 'baseline': values[0x1017,0] = 500
    if case == 'moving': values[0x606C,1] = 21
    if case == 'enabled': values[0x6041,0] = 0x00270027
    before = values.copy()
    configure_pdo = case not in ('unchanged','keep')
    bus = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    bus.bind(('vcan0',)); bus.setblocking(False)
    with tempfile.TemporaryDirectory() as directory:
        conf = Path(directory)/'device.conf'
        lines = []
        replacements = {name: str(desired[key]) if key in desired else 'keep' for key,(name,_) in PARAMETERS.items()}
        replacements.update(interface='vcan0',drive_pdo_profile=str(int(configure_pdo)))
        for line in Path(os.environ['TEST_APP_CONFIG']).read_text().splitlines():
            key = line.partition('=')[0]
            lines.append(key+'='+replacements[key] if key in replacements else line)
        conf.write_text('\n'.join(lines)+'\n')
        command = [os.environ['DEVICE_CONFIG_PROGRAM'],'--config',str(conf)]
        dry = subprocess.run(command,capture_output=True,timeout=3)
        assert dry.returncode == 0 and b'CONFIG_VALID_DEVICE_ACCESS_NONE' in dry.stdout, dry
        assert not select.select([bus],[],[],0)[0], 'validation must not send CAN'
        writes=[]; save_count=0; nmt=127; injected=False
        process = subprocess.Popen(command+['--apply'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        os.set_blocking(process.stdout.fileno(),False)
        output=bytearray(); deadline=time.monotonic()+35

        def send(identifier,payload):
            """Send only on the namespace-owned vcan0 peer."""
            bus.send(struct.pack('=IB3x8s',identifier,len(payload),payload.ljust(8,b'\0')))

        try:
            while process.poll() is None and time.monotonic()<deadline:
                for ready in select.select([bus,process.stdout],[],[],.01)[0]:
                    if ready is process.stdout:
                        output.extend(os.read(process.stdout.fileno(),65536)); continue
                    raw=bus.recv(16); identifier,size,payload=struct.unpack('=IB3x8s',raw)
                    if identifier == 0:
                        assert size==2 and payload[:2]==b'\x80\x01' and values[0x1017,0]==500
                        nmt=127; send(0x701,bytes([nmt])); continue
                    assert identifier==0x601 and size==8, ('unexpected traffic',raw)
                    cmd=payload[0]; key=int.from_bytes(payload[1:3],'little'),payload[3]
                    if cmd==0x40:
                        assert key in values,key
                        if case=='unreadable' and key==(0x6083,2):
                            send(0x581,b'\x80'+payload[1:4]+struct.pack('<I',0x06020000)); continue
                        send(0x581,bytes([{1:0x4f,2:0x4b,4:0x43}[width(key)]])+payload[1:4]+values[key].to_bytes(4,'little'))
                        continue
                    value=int.from_bytes(payload[4:],'little'); writes.append((key,value))
                    if key==(0x2010,0):
                        assert cmd==0x2b and value==1
                        save_count+=1; assert save_count==1
                        assert values[0x1017,0]==0 and values[0x2000,0]==0
                        assert all(values[k]==v for k,v in desired.items()),(case,values,desired)
                        assert all(values[k]==v for k,v in pdo_expected.items())
                        if case=='save_timeout': continue
                        if case=='save_abort':
                            send(0x581,b'\x80'+payload[1:4]+struct.pack('<I',0x08000020)); continue
                    else:
                        assert cmd=={1:0x2f,2:0x2b,4:0x23}[width(key)],(key,payload)
                        assert key in desired or key in pdo_expected or key==(0x1017,0),key
                        if key in pdo_expected:
                            assert nmt==127 and configure_pdo
                            if key[0] in (0x1600,0x1A00,0x1A01):
                                comm={0x1600:0x1400,0x1A00:0x1800,0x1A01:0x1801}[key[0]]
                                assert values[comm,1]&0x80000000, ('mapping while enabled',key)
                                if key[1]!=0: assert values[key[0],0]==0
                            if case=='pdo_abort' and key==(0x1A00,1):
                                send(0x581,b'\x80'+payload[1:4]+struct.pack('<I',0x06040041)); continue
                        if key in desired: assert value==desired[key]
                        if key==(0x1017,0): assert value in (0,500)
                        if key==(0x2026,5) and case=='write_abort':
                            send(0x581,b'\x80'+payload[1:4]+struct.pack('<I',0x06090030)); continue
                        if not (case=='readback_mismatch' and key==(0x2026,5)): values[key]=value
                        if key==(0x6060,0): values[0x6061,0]=value
                        if key==(0x2026,5):
                            if case=='write_timeout': continue
                            if case=='boot': send(0x701,b'\x00'); injected=True
                    send(0x581,b'\x60'+payload[1:4]+bytes(4))
                    if key==(0x1017,0) and value==500: send(0x701,bytes([nmt]))
            process.wait(timeout=2); output.extend(process.stdout.read() or b'')
            success=case in ('success','unchanged','keep','pdo')
            assert process.returncode==(0 if success else 1),(case,output)
            assert (b'EEPROM_SAVE_ACKNOWLEDGED RESTART_REQUIRED' in output)==success,(case,output)
            assert save_count==(1 if success or case.startswith('save_') else 0),(case,writes,output)
            if case in ('baseline','moving','enabled','unreadable'): assert not writes
            if case in ('unchanged','keep'): assert writes==[((0x2010,0),1)],writes
            if case=='keep': assert all(values[k]==before[k] for k in PARAMETERS if k not in desired)
            if case=='boot': assert injected
            print('PASS device configuration:',case)
        finally:
            if process.poll() is None: process.kill(); process.wait()
            process.stdout.close(); bus.close()


def main():
    """Exercise persistent fields, PDO preparation, untouched fields and failures without hardware."""
    for case in ('success','pdo','unchanged','keep','baseline','moving','enabled','unreadable','write_abort',
                 'readback_mismatch','write_timeout','pdo_abort','save_abort','save_timeout','boot'):
        trial(case)


if __name__=='__main__': main()
