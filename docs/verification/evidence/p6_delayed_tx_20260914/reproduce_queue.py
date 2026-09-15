"""Demonstrate queued CAN TX after closing its socket, only on isolated vcan0."""
import json
import os
import socket
import struct
import subprocess
import time

assert os.environ.get('ROBOT_CONTROL_ISOLATED_QUEUE_TEST') == '1'
links = json.loads(subprocess.check_output(['ip', '-j', '-details', 'link', 'show'], text=True))
assert {entry['ifname'] for entry in links} == {'lo', 'vcan0'}
assert next(entry for entry in links if entry['ifname'] == 'vcan0')['linkinfo']['info_kind'] == 'vcan'
receiver = socket.socket(socket.PF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
receiver.bind(('vcan0',))
receiver.settimeout(2)
sender = socket.socket(socket.PF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
sender.bind(('vcan0',))
payload = bytes.fromhex('406c600200000000')
frame = struct.pack('=IB3x8s', 0x601, 8, payload)
started = time.monotonic_ns()
assert sender.send(frame) == 16
submitted = time.monotonic_ns()
sender.close()
closed = time.monotonic_ns()
received = receiver.recv(16)
arrived = time.monotonic_ns()
receiver.close()
assert received == frame
assert arrived - closed >= 200_000_000, 'No delayed transmission was demonstrated'
print(json.dumps({'interface': 'isolated vcan0', 'sent_frames': 1, 'received_frames': 1,
    'frame': '601#' + payload.hex(), 'submit_us': (submitted-started)/1000,
    'close_after_submit_us': (closed-submitted)/1000,
    'receive_after_close_ms': (arrived-closed)/1_000_000,
    'result': 'QUEUED_FRAME_DELIVERED_AFTER_SOCKET_CLOSE',
    'limit': 'netem qdisc demonstration; not the RK3588 controller implementation'}))
