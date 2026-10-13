#!/usr/bin/env python3
"""Exercise installed ros2 run/launch from outside the checkout, with real DDS and no devices."""
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import rclpy  # pyright: ignore[reportMissingImports]
from rclpy.qos import (  # pyright: ignore[reportMissingImports]
    DurabilityPolicy,
    QoSProfile,
    ReliabilityPolicy,
)
from std_msgs.msg import String  # pyright: ignore[reportMissingImports]


def main():
    """Check missing socket, 500ms silence, same-server recovery, node/server restarts and SIGTERM."""
    fixture = str(Path(sys.argv[1]).resolve())
    mode = sys.argv[2]
    assert mode in ('run', 'launch')
    rclpy.init()
    observer = rclpy.create_node('installed_diagnostics_test')
    qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.BEST_EFFORT,
                     durability=DurabilityPolicy.VOLATILE)
    reports = []
    subscription = observer.create_subscription(String, '/installed/robot_control_ipc_monitor/diagnostics',
                                                lambda msg: reports.append(json.loads(msg.data)), qos)
    processes = []
    with tempfile.TemporaryDirectory() as directory:
        path = str(Path(directory) / 'control.sock')

        def start(role):
            """Start installed programs with no /clock, no source imports and an isolated process group."""
            profile = Path(directory) / 'dds.xml'
            profile.write_text('<profiles xmlns="http://www.eprosima.com/XMLSchemas/fastRTPS_Profiles"/>')
            if mode == 'launch':
                args = ['ros2', 'launch', 'robot_control_ros2', 'diagnostics.launch.py',
                        f'role:={role}', f'socket_path:={path}', 'use_sim_time:=true',
                        'namespace:=/installed', f'dds_profile:={profile}']
            else:
                args = ['ros2', 'run', 'robot_control_ros2',
                        'robot_control_ipc_node' if role == 'bridge' else 'robot_control_ipc_monitor',
                        '--ros-args', '-p', 'use_sim_time:=true', '-r', '__ns:=/installed']
                if role == 'bridge':
                    args += ['-p', f'socket_path:={path}']
            process = subprocess.Popen(args, cwd=directory, start_new_session=True)
            processes.append(process)
            return process

        def wait_for(predicate, timeout=5):
            """Observe a new diagnostics state with a bounded deadline while spinning DDS."""
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                rclpy.spin_once(observer, timeout_sec=.02)
                if reports and predicate(reports[-1]):
                    return reports[-1]
            raise AssertionError(reports[-5:])

        def stop(process):
            """Signal the whole CLI/child group and require exit; the independent fixture stays alive."""
            os.killpg(process.pid, signal.SIGTERM)
            assert process.wait(timeout=4) in (0, -signal.SIGTERM), 'unexpected CLI shutdown'

        def start_server():
            """Start only the device-free C++ IPC server; its endpoint is private and automatically removed."""
            server = subprocess.Popen([fixture, path], cwd=directory, stdout=subprocess.PIPE, text=True)
            processes.append(server)
            assert server.stdout is not None and server.stdout.readline() == 'READY\n'
            return server

        try:
            monitor = start('monitor')
            wait_for(lambda row: row['state'] == 'WAITING')
            bridge = start('bridge')
            wait_for(lambda row: row['state'] == 'DISCONNECTED')
            server = start_server()
            first = wait_for(lambda row: row['state'] == 'FRESH' and row['healthy'])
            assert first['source_age_ms'] is None and first['source_age_verified'] is False
            # Freeze only the producer, while server and host monitor remain alive.
            os.killpg(bridge.pid, signal.SIGSTOP)
            stale = wait_for(lambda row: row['state'] == 'STALE' and row['receipt_age_ms'] >= 500)
            assert not stale['healthy'] and server.poll() is None
            os.killpg(bridge.pid, signal.SIGCONT)
            recovered = wait_for(lambda row: row['state'] == 'FRESH' and row['cycles'] > first['cycles'])
            assert recovered['server_started_ns'] == first['server_started_ns']
            assert server.poll() is None, 'silence overlapped fixture exit'
            stop(bridge)
            assert server.poll() is None, 'ROS exit stopped the independent fixture'
            wait_for(lambda row: row['state'] == 'STALE')
            bridge = start('bridge')
            wait_for(lambda row: row['state'] == 'FRESH' and row['session'] > recovered['session'])
            assert server.wait(timeout=9) == 0
            wait_for(lambda row: row['state'] == 'DISCONNECTED')
            server = start_server()
            wait_for(lambda row: row['state'] == 'FRESH'
                     and row['server_started_ns'] != first['server_started_ns'])
            assert all(row['executed'] is False and row['motion_authority'] is False for row in reports)
            stop(bridge)
            stop(monitor)
            print(f'PASS: installed {mode}, non-source cwd, missing socket, paused /clock, 500ms expiry, '
                  'same-server recovery, bridge/server restart and SIGTERM')
        finally:
            for process in processes:
                if process.poll() is None:
                    try:
                        # A stopped process needs SIGCONT before handling cleanup signals.
                        os.killpg(process.pid, signal.SIGCONT)
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        process.kill()
                    process.wait(timeout=2)
            observer.destroy_subscription(subscription)
            observer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    main()
