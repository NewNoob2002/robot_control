#!/usr/bin/env python3
"""Observe a device-free board fixture over DDS; freshness uses host receipt time, never remote clock subtraction."""
import argparse
import json
import time
from collections import Counter
from pathlib import Path

import rclpy  # pyright: ignore[reportMissingImports]
from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy  # pyright: ignore[reportMissingImports]
from std_msgs.msg import String  # pyright: ignore[reportMissingImports]


def main():
    """Check fixture identity, advancing status, expiry during silence and recovery across server instances."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--topic', default='/verification/ipc_verify/status')
    parser.add_argument('--duration-sec', type=float, default=30)
    parser.add_argument('--min-instances', type=int, default=2)
    parser.add_argument('--min-stale-events', type=int, default=1)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--network-events', type=Path)
    args = parser.parse_args()
    if not 1 <= args.duration_sec <= 60 or not 1 <= args.min_instances <= 4 or args.min_stale_events < 0:
        parser.error('duration must be 1..60 seconds, instances 1..4 and stale events nonnegative')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    counts = Counter()
    received_samples = []
    stale_times = []
    previous = {}
    last_received = None
    fresh = False
    stale_events = 0
    rclpy.init()
    node = rclpy.create_node('ipc_remote_status_observer')
    qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.BEST_EFFORT, durability=DurabilityPolicy.VOLATILE)
    with args.output.with_suffix('.jsonl').open('w') as log:
        def received(message):
            """Validate independent fixture values and track host-local progress, without publishing commands."""
            nonlocal last_received, fresh
            value = json.loads(message.data)
            now = time.monotonic()
            assert value.get('executed') is False and value.get('motion_authority') is False, value
            log.write(json.dumps({'host_received_monotonic': now, 'status': value}) + '\n')
            log.flush()
            if value.get('connected') is not True or 'cycles' not in value:
                return
            assert value.get('version') == 1 and value.get('result') == 'status', value
            assert value['source'] == value['left_target_rpm'] == value['right_target_rpm'] == 0, value
            assert value['left_feedback_tenths_rpm'] == -2 and value['healthy'] is True, value
            fields = ('server_started_ns', 'session', 'sequence', 'captured_ns', 'cycles')
            assert all(type(value[key]) is int and value[key] > 0 for key in fields), value
            key = value['server_started_ns'], value['session']
            current = value['sequence'], value['captured_ns'], value['cycles']
            if key in previous:
                assert all(b > a for a, b in zip(previous[key], current, strict=True)), value
            previous[key] = current
            counts[value['server_started_ns']] += 1
            received_samples.append((value['server_started_ns'], now))
            last_received = now
            if not fresh:
                print('FRESH', value['server_started_ns'], flush=True)
            fresh = True

        subscription = node.create_subscription(String, args.topic, received, qos)
        print('READY', flush=True)
        deadline = time.monotonic() + args.duration_sec
        try:
            while time.monotonic() < deadline:
                rclpy.spin_once(node, timeout_sec=.02)
                if fresh and last_received is not None and time.monotonic() - last_received >= .5:
                    fresh = False
                    stale_events += 1
                    stale_times.append(time.monotonic())
                    print('STALE', flush=True)
            publishers = [{'node': item.node_name, 'namespace': item.node_namespace}
                          for item in node.get_publishers_info_by_topic(args.topic)]
            result = {'samples_by_server': dict(counts), 'stale_events': stale_events,
                      'final_fresh': fresh, 'publishers': publishers, 'expiry_sec': .5,
                      'source_age_verified': False, 'cross_host_monotonic_comparison': False}
            result['passed'] = (len(counts) >= args.min_instances
                                and all(count >= 20 for count in counts.values())
                                and stale_events >= args.min_stale_events)
            if args.network_events is not None:
                events = json.loads(args.network_events.read_text())
                down, up = events['disconnected_monotonic'], events['reconnected_monotonic']
                server = events['interrupted_server_started_ns']
                before = [stamp for instance, stamp in received_samples if instance == server and stamp < down]
                after = [stamp for instance, stamp in received_samples if instance == server and stamp > up]
                network_passed = (up - down >= 1.5 and bool(before) and bool(after)
                                  and down - max(before) < .2
                                  and not any(down + .5 <= stamp < up for _, stamp in received_samples)
                                  and any(down <= stamp < up for stamp in stale_times))
                result['network_interruption_passed'] = network_passed
                result['network_interruption_sec'] = up - down
                result['same_server_recovery'] = bool(after)
                if after:
                    result['reconnect_to_sample_sec'] = min(after) - up
                result['passed'] = result['passed'] and network_passed
            args.output.write_text(json.dumps(result, indent=2) + '\n')
            assert result['passed'], result
            print('PASS', json.dumps(result), flush=True)
        finally:
            node.destroy_subscription(subscription)
            node.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    main()
