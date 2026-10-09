#!/usr/bin/env python3
"""Humble DDS-to-Unix-socket integration using the device-free C++ fixture."""
import importlib.util
import json
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from unittest.mock import patch

# ROS imports are verified in the pinned Humble image; host analysis does not require ROS.
import rclpy  # pyright: ignore[reportMissingImports]
from rclpy.impl.implementation_singleton import (  # pyright: ignore[reportMissingImports]
    rclpy_implementation,
)
from rclpy.qos import (  # pyright: ignore[reportMissingImports]
    DurabilityPolicy,
    QoSProfile,
    ReliabilityPolicy,
)
from std_msgs.msg import String  # pyright: ignore[reportMissingImports]


def check_shutdown_race(script):
    """Reproduce shutdown between executor checks; an error with a live context must still fail."""
    spec = importlib.util.spec_from_file_location('ipc_shutdown_check', script)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    arguments = ['--ros-args', '-p', 'socket_path:=/tmp/unused-ipc-shutdown-check.sock']

    def stopped_context(_node):
        """Model Humble's asynchronous SIGTERM context invalidation before wait-set creation."""
        rclpy.shutdown()
        raise rclpy_implementation.RCLError('the given context is not valid')

    with patch.object(rclpy, 'spin', side_effect=stopped_context):
        module.main(arguments)
    with patch.object(rclpy, 'spin', side_effect=rclpy_implementation.RCLError('live context failure')):
        try:
            module.main(arguments)
        except rclpy_implementation.RCLError:
            pass
        else:
            raise AssertionError('a live-context ROS failure was hidden')
    print('PASS: shutdown/context race and live-context error propagation')


def main():
    """Verify ROS probes, SIGTERM, node restart, server loss and automatic read-only reconnection."""
    fixture = str(Path(sys.argv[1]).resolve())
    script = Path(__file__).resolve().parents[2] / 'ros2/robot_control_ipc_node.py'
    check_shutdown_race(script)
    rclpy.init()
    observer = rclpy.create_node('ipc_test_observer')
    qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.BEST_EFFORT, durability=DurabilityPolicy.VOLATILE)
    replies = []
    subscription = observer.create_subscription(String, '/robot_control_ipc/status',
                                                lambda msg: replies.append(json.loads(msg.data)), qos)
    publisher = observer.create_publisher(String, '/robot_control_ipc/probe', qos)
    server = node = None
    with tempfile.TemporaryDirectory() as directory:
        path = str(Path(directory) / 'control.sock')

        def start_server():
            """Start the real IPC implementation without constructing a hardware control loop."""
            process = subprocess.Popen([fixture, path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            assert process.stdout is not None and process.stderr is not None
            assert process.stdout.readline() == 'READY\n', process.stderr.read()
            return process

        def start_node(use_sim_time=False):
            """Start an independent ROS executor, optionally with a paused simulation clock and no /clock."""
            return subprocess.Popen([sys.executable, str(script), '--ros-args', '-p', f'socket_path:={path}',
                                     '-p', f'use_sim_time:={str(use_sim_time).lower()}'])

        def wait_for(predicate, timeout=5, probe=False):
            """Drive DDS with an overall deadline and freshly sampled probe timestamps."""
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if predicate():
                    return
                if probe:
                    publisher.publish(String(data=json.dumps({'captured_ns': time.monotonic_ns(),
                                                              'left_rpm': 5, 'right_rpm': -5})))
                rclpy.spin_once(observer, timeout_sec=.02)
            raise AssertionError(replies[-5:])

        try:
            server = start_server()
            node = start_node()
            wait_for(lambda: any(reply.get('connected') for reply in replies))
            wait_for(lambda: any(reply.get('result') == 'probe_ok' for reply in replies), probe=True)
            connected = [reply for reply in replies if reply.get('connected') and 'cycles' in reply]
            assert all(reply['executed'] is False and reply['motion_authority'] is False for reply in connected)
            assert all(reply['left_target_rpm'] == reply['right_target_rpm'] == 0 for reply in connected)
            generation = connected[-1]['server_started_ns']
            node.send_signal(signal.SIGTERM)
            assert node.wait(timeout=3) == 0
            assert server.poll() is None, 'ROS exit stopped the server'
            replies.clear()
            node = start_node(use_sim_time=True)
            wait_for(lambda: any(reply.get('connected') for reply in replies))
            wait_for(lambda: any(reply.get('result') == 'probe_ok' for reply in replies), probe=True)
            assert server.wait(timeout=9) == 0
            replies.clear()
            wait_for(lambda: any(reply.get('connected') is False for reply in replies))
            server = start_server()
            wait_for(lambda: any(reply.get('server_started_ns', generation) != generation for reply in replies))
            node.send_signal(signal.SIGTERM)
            assert node.wait(timeout=3) == 0
            assert server.wait(timeout=9) == 0
            print('PASS: Humble DDS status/probe, SIGTERM, node restart, paused simulation clock, '
                  'server loss and reconnection')
        finally:
            for process in (node, server):
                if process is not None and process.poll() is None:
                    process.kill()
                    process.wait(timeout=2)
            observer.destroy_subscription(subscription)
            observer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    main()
