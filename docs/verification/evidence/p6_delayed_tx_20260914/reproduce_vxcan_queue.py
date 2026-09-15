"""Demonstrate queued CAN TX after socket close on an isolated vxcan pair."""
import json
import os
import socket
import struct
import subprocess
import sys
import time

assert os.environ.get('ROBOT_CONTROL_ISOLATED_QUEUE_TEST') == '1'
links = json.loads(subprocess.check_output(['ip', '-j', '-details', 'link', 'show'], text=True))
assert {entry['ifname'] for entry in links} == {'lo', 'vxcan0', 'vxcan1'}
assert next(entry for entry in links if entry['ifname'] == 'vxcan0')['linkinfo']['info_kind'] == 'vxcan'
receiver = socket.socket(socket.PF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
receiver.bind(('vxcan1',))
receiver.settimeout(2)
sender = socket.socket(socket.PF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
sender.bind(('vxcan0',))
sender.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_LOOPBACK, 0)
payload = bytes.fromhex('406c600200000000')
frame = struct.pack('=IB3x8s', 0x601, 8, payload)
started = time.monotonic_ns()
assert sender.send(frame) == 16
submitted = time.monotonic_ns()
sender.close()
closed = time.monotonic_ns()
if sys.argv[1:] == ['--link-down']:
    subprocess.run(['ip', 'link', 'set', 'vxcan0', 'down'], check=True, timeout=2)
    try:
        receiver.recv(16)
    except TimeoutError:
        print(json.dumps({'interface': 'isolated vxcan pair', 'sent_frames': 1,
            'result': 'NO_FRAME_AFTER_LINK_DOWN', 'receive_timeout_seconds': 2,
            'limit': 'netem qdisc only; real controller cancellation is unverified'}))
        receiver.close()
        raise SystemExit(0)
    raise AssertionError('Queued frame survived link-down')
assert not sys.argv[1:]
received = receiver.recv(16)
arrived = time.monotonic_ns()
receiver.close()
assert received == frame
assert arrived - closed >= 200_000_000, 'No delayed transmission was demonstrated'
print(json.dumps({'interface': 'isolated vxcan pair', 'sent_frames': 1, 'received_frames': 1,
    'frame': '601#' + payload.hex(), 'submit_us': (submitted-started)/1000,
    'close_after_submit_us': (closed-submitted)/1000,
    'receive_after_close_ms': (arrived-closed)/1_000_000,
    'result': 'QUEUED_FRAME_DELIVERED_AFTER_SOCKET_CLOSE',
    'limit': 'netem qdisc demonstration; not the RK3588 controller implementation'}))
