import base64
import io
import json
import socket
import struct
import sys
import time
from collections import Counter


def locators(payload):
    """Decode bounded SPDP DATA parameter-list UDP locators; unsupported encodings are left undecoded."""
    if not payload.startswith(b'RTPS') or len(payload)<20:
        return []
    # ponytail: decode plain PL_CDR_LE SPDP only; use Wireshark for other encodings.
    found=[]
    offset=20
    while offset+4<=len(payload):
        identifier,flags=payload[offset:offset+2]
        endian='<' if flags&1 else '>'
        size=struct.unpack_from(endian+'H',payload,offset+2)[0]
        body=payload[offset+4:offset+4+size] if size else payload[offset+4:]
        if identifier==0x15 and not flags&2 and len(body)>=24 and body[8:12]==b'\x00\x01\x00\xc2' and body[20:22]==b'\x00\x03':
            cursor=24
            while cursor+4<=len(body):
                pid,length=struct.unpack_from('<HH',body,cursor)
                if pid==1 or cursor+4+length>len(body):
                    break
                value=body[cursor+4:cursor+4+length]
                if pid in (0x31,0x32,0x33,0x48) and length==24:
                    kind,port=struct.unpack_from('<iI',value)
                    if kind==1:
                        found.append((pid,socket.inet_ntoa(value[20:24]),port))
                cursor+=4+((length+3)&~3)
        if not size:
            break
        offset+=4+size
    return found


# Small self-check covers locator identity and a truncated/unrelated packet.
value=struct.pack('<iI',1,54710)+bytes(12)+socket.inet_aton('192.0.2.10')
body=struct.pack('<HH',0,16)+bytes(4)+b'\x00\x01\x00\xc2'+bytes(8)+b'\x00\x03\x00\x00'+struct.pack('<HH',0x32,24)+value+struct.pack('<HH',1,0)
wire=b'RTPS'+bytes(16)+bytes((0x15,5))+struct.pack('<H',len(body))+body
assert locators(wire)==[(0x32,'192.0.2.10',54710)] and locators(b'RTPS')==[]
if len(sys.argv)==2 and sys.argv[1]=='--self-check':
    print('LOCATOR_DECODER_SELF_CHECK_PASS')
    raise SystemExit(0)

interface,peer,local,prefix=sys.argv[1:5]
duration=float(sys.argv[5]) if len(sys.argv)>5 else 40
assert 1<=duration<=40
counts=Counter()
advertised=set()
started=time.monotonic()
with socket.socket(socket.AF_PACKET,socket.SOCK_RAW,socket.htons(0x0003)) as source, io.BytesIO() as capture:
    source.bind((interface,0))
    source.settimeout(.2)
    capture.write(struct.pack('<IHHIIII',0xa1b2c3d4,2,4,0,0,65535,1))
    print('CAPTURE_READY',flush=True)
    while time.monotonic()-started<duration:
        try:
            packet=source.recv(65535)
        except socket.timeout:
            continue
        if len(packet)<42 or packet[12:14]!=b'\x08\x00' or packet[23]!=17:
            continue
        header=(packet[14]&15)*4
        offset=14+header
        if header<20 or offset+8>len(packet) or struct.unpack_from('!H',packet,20)[0]&0x1fff:
            continue
        sender,receiver=socket.inet_ntoa(packet[26:30]),socket.inet_ntoa(packet[30:34])
        sport,dport,length,_=struct.unpack_from('!HHHH',packet,offset)
        payload=packet[offset+8:offset+length]
        if (not (peer in (sender,receiver) or sender==local and receiver=='239.255.0.1')
                or not (53900<=dport<=53930 or 54710<=dport<=54721 or dport==54820)
                or not payload.startswith((b'RTPS',b'RC_UDP_DIAG '))):
            continue
        now=time.time()
        # ponytail: bound capture to 2MiB; use tcpdump for long captures.
        if capture.tell()+len(packet)+16<=2*1024*1024:
            capture.write(struct.pack('<IIII',int(now),int(now%1*1_000_000),len(packet),len(packet))+packet)
        counts[f'{sender}:{sport}->{receiver}:{dport}']+=1
        for pid,address,port in locators(payload):
            advertised.add((sender,pid,address,port))
    pcap=capture.getvalue()
print(json.dumps({'counts':dict(counts),
    'spdp_udp_locators':[dict(packet_source=sender,pid=pid,address=address,port=port)
                         for sender,pid,address,port in sorted(advertised)],
    'duration_sec':time.monotonic()-started,'interface':interface,'promiscuous':False,
    'pcap_base64':base64.b64encode(pcap).decode('ascii')}),flush=True)
