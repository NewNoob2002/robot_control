#!/usr/bin/env python3
"""Independent wire checks against the real C++ endpoint, no CAN/UART or ROS dependency."""
import importlib.util
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time


def main():
    """Check validation, peer lifecycle, bounded backlog and preservation of existing endpoints."""
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory() as directory:
        path = str(Path(directory) / 'control.sock')
        existing = Path(path)
        existing.write_text('keep')
        failed = subprocess.run([binary, path], capture_output=True, timeout=2)
        assert failed.returncode == 1 and existing.read_text() == 'keep'
        existing.unlink()
        os.chmod(directory, 0o755)
        assert subprocess.run([binary, path], capture_output=True, timeout=2).returncode == 1
        assert not existing.exists()
        os.chmod(directory, 0o700)
        process = subprocess.Popen([binary, path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            assert process.stdout.readline() == 'READY\n'
            assert existing.stat().st_mode & 0o777 == 0o600

            def connect():
                """Open one real local peer with a bounded test-only receive timeout."""
                peer = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
                peer.settimeout(1)
                peer.connect(path)
                return peer

            def exchange(peer, packet, result):
                """Verify exact server validation and immutable, non-executed status."""
                peer.sendall(packet.encode())
                reply = json.loads(peer.recv(1024))
                assert reply['result'] == result, reply
                assert reply['executed'] is False and reply['motion_authority'] is False
                assert reply['source'] == 0 and reply['left_target_rpm'] == reply['right_target_rpm'] == 0
                assert reply['left_feedback_tenths_rpm'] == -2 and reply['healthy']
                return reply

            def packet(seq, left=0, right=0, stamp=None, kind=1):
                """Encode independent protocol vectors using Linux monotonic nanoseconds."""
                return f'RCI1 {seq} {time.monotonic_ns() if stamp is None else stamp} {kind} {left} {right}'

            with connect() as peer:
                first = exchange(peer, packet(1, kind=0), 'status')
                exchange(peer, packet(2, 5, -5), 'probe_ok')
                exchange(peer, packet(2), 'sequence_replay')
                exchange(peer, packet(1), 'sequence_replay')
                exchange(peer, packet(3, stamp=time.monotonic_ns() - 100_000_000), 'stale_or_future')
                exchange(peer, packet(4, stamp=time.monotonic_ns() + 1_000_000_000), 'stale_or_future')
                exchange(peer, packet(5, 6), 'rpm_range')
                for raw in ('RCI2 6 1 1 0 0', packet(6) + ' extra', packet(6) + ' ',
                            'RCI1 6 1 1 nan 0', 'RCI1 -1 1 1 0 0', 'RCI1 6 1 1 0',
                            'RCI1 18446744073709551616 1 1 0 0', packet(6, 1, kind=0)):
                    exchange(peer, raw, 'malformed')
                with connect() as second:
                    assert second.recv(1024) == b''
                exchange(peer, packet(6), 'probe_ok')
                peer.sendall(b'x' * 193)
                assert peer.recv(1024) == b''
            time.sleep(.02)
            with connect() as peer:
                newer = exchange(peer, packet(1), 'probe_ok')
                assert newer['session'] > first['session'] and newer['cycles'] > first['cycles']
                # Stop reading responses: server must shed this peer, not stall its owner.
                peer.setblocking(False)
                dropped = False
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline:
                    try:
                        peer.send(packet(2).encode())
                    except BlockingIOError:
                        pass
                    except (BrokenPipeError, ConnectionResetError):
                        dropped = True
                        break
                    time.sleep(.001)
                assert dropped, 'backpressured peer was not disconnected'
            time.sleep(.02)
            module_path = Path(__file__).resolve().parents[2] / 'ros2/robot_control_ipc_node.py'
            spec = importlib.util.spec_from_file_location('ipc_node', module_path)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            client = module.Client(path)
            try:
                deadline = time.monotonic() + 1
                seen = []
                submitted = False
                while time.monotonic() < deadline:
                    reply = client.tick()
                    if reply:
                        assert reply['connected'], reply
                        seen.append(reply)
                        if not submitted:
                            client.submit_probe(json.dumps({'captured_ns': time.monotonic_ns(),
                                                           'left_rpm': 5, 'right_rpm': -5}))
                            submitted = True
                        if reply['result'] == 'probe_ok':
                            break
                    time.sleep(.005)
                assert any(item['result'] == 'probe_ok' for item in seen)
                assert seen[-1]['cycles'] > newer['cycles']
                for bad in ('null', '{}', '{"captured_ns":1,"left_rpm":0,"right_rpm":0}',
                            '{"captured_ns":true,"left_rpm":0,"right_rpm":0}'):
                    try:
                        client.submit_probe(bad)
                    except ValueError:
                        pass
                    else:
                        raise AssertionError(bad)
            finally:
                client.close()
            # A connected but silent observer cannot occupy the only slot indefinitely.
            time.sleep(.02)
            with connect() as peer:
                time.sleep(1.1)
                assert peer.recv(1024) == b''
            assert process.wait(timeout=9) == 0, process.stderr.read()
            assert not existing.exists()
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=2)
    print('PASS: read-only IPC validation, replay, age, peer restart, backpressure, idle, cleanup and client')


if __name__ == '__main__':
    main()
