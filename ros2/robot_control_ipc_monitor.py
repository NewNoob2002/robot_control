#!/usr/bin/env python3
"""Show host-local DDS receipt freshness; observations never grant motion authority."""
import json
import time


class ReceiptState:
    """Keep one diagnostic observation, with an injected local monotonic clock for tests."""

    def __init__(self, clock=time.monotonic_ns):
        """Start without a receipt; the fixed expiry is 500 ms regardless of ROS /clock."""
        self.clock = clock
        self.received_at = None
        self.snapshot = None
        self.connected = False
        self.error = None

    def receive(self, raw):
        """Validate one bounded JSON status; repeated same-session revisions cannot refresh age."""
        try:
            if type(raw) is not str or len(raw) > 4096 or len(raw.encode('utf-8')) > 4096:
                raise ValueError('status must be a string of at most 4096 bytes')
            value = json.loads(raw)
            if (not isinstance(value, dict) or type(value.get('version')) is not int or value['version'] != 1
                    or type(value.get('connected')) is not bool
                    # Exact booleans are required, not integer zero.
                    # pi-lens-ignore: no-identity-operator-on-literals
                    or value.get('executed') is not False or value.get('motion_authority') is not False):
                raise ValueError('invalid diagnostic status envelope')
            # Probe validation errors are events, not new control snapshots or receipts.
            if 'probe_error' in value:
                return
            if not value['connected']:
                self.connected = False
                self.error = str(value.get('error', 'IPC disconnected'))[:256]
                return
            for name in ('server_started_ns', 'session', 'sequence', 'captured_ns', 'cycles'):
                if type(value.get(name)) is not int or value[name] < (0 if name == 'cycles' else 1):
                    raise ValueError(f'invalid {name}')
            if type(value.get('healthy')) is not bool:
                raise ValueError('invalid healthy flag')
            if self.snapshot is not None:
                same_session = all(value[key] == self.snapshot[key] for key in ('server_started_ns', 'session'))
                if same_session and (value['sequence'] <= self.snapshot['sequence']
                                     or value['captured_ns'] <= self.snapshot['captured_ns']
                                     or value['cycles'] < self.snapshot['cycles']):
                    raise ValueError('repeated or regressed diagnostic revision')
            self.snapshot = value
            self.received_at = self.clock()
            self.connected = True
            self.error = None
        except (ValueError, TypeError, UnicodeError, RecursionError) as error:
            self.connected = False
            self.error = f'invalid status: {error}'[:256]

    def report(self):
        """Expire at exactly 500 ms; never compare remote timestamps with the local clock."""
        age = None if self.received_at is None else self.clock() - self.received_at
        stale = age is None or age < 0 or age >= 500_000_000
        state = ('INVALID' if self.error and self.error.startswith('invalid status:') else
                 'DISCONNECTED' if not self.connected and self.error else
                 'WAITING' if age is None else 'STALE' if stale else 'FRESH')
        return {'version': 1, 'state': state,
                'connected': True if state == 'FRESH' else False if state == 'DISCONNECTED' else None,
                'ipc_connected_reported': self.connected,
                'receipt_age_ms': None if age is None else age / 1_000_000,
                'receipt_expiry_ms': 500, 'receipt_expired': stale, 'stale': state != 'FRESH',
                'healthy': state == 'FRESH' and bool(self.snapshot and self.snapshot['healthy']),
                'source_age_ms': None, 'source_age_verified': False,
                'executed': False, 'motion_authority': False, 'error': self.error,
                'server_started_ns': None if self.snapshot is None else self.snapshot['server_started_ns'],
                'session': None if self.snapshot is None else self.snapshot['session'],
                'cycles': None if self.snapshot is None else self.snapshot['cycles']}


def main(args=None):
    """Publish ~/diagnostics at 10 Hz and print state changes/one-second updates locally."""
    # ROS runtime imports are checked in the pinned Humble image, not the host analyzer.
    import rclpy  # pyright: ignore[reportMissingImports]
    from rclpy import signals  # pyright: ignore[reportMissingImports]
    from rclpy.clock import Clock, ClockType  # pyright: ignore[reportMissingImports]
    from rclpy.impl.implementation_singleton import (  # pyright: ignore[reportMissingImports]
        rclpy_implementation,
    )
    from rclpy.node import Node  # pyright: ignore[reportMissingImports]
    from rclpy.qos import (  # pyright: ignore[reportMissingImports]
        DurabilityPolicy,
        QoSProfile,
        ReliabilityPolicy,
    )
    from std_msgs.msg import String  # pyright: ignore[reportMissingImports]

    class Monitor(Node):
        """One read-only subscriber; the final display computes freshness in its own clock domain."""

        def __init__(self):
            """Use volatile depth-one DDS and a steady timer even under paused simulation time."""
            super().__init__('robot_control_ipc_monitor')
            self.declare_parameter('status_topic', 'robot_control_ipc/status')
            self.state = ReceiptState()
            qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.BEST_EFFORT,
                             durability=DurabilityPolicy.VOLATILE)
            self.status = self.create_subscription(String, self.get_parameter('status_topic').value,
                                                   lambda message: self.state.receive(message.data), qos)
            self.diagnostics = self.create_publisher(String, '~/diagnostics', qos)
            self.last_state = None
            self.print_at = 0
            self.timer = self.create_timer(0.1, self.tick, clock=Clock(clock_type=ClockType.STEADY_TIME))

        def tick(self):
            """Publish the effective health and local age; retain no healthy display after expiry."""
            report = self.state.report()
            text = json.dumps(report, allow_nan=False)
            self.diagnostics.publish(String(data=text))
            now = time.monotonic_ns()
            if report['state'] != self.last_state or now >= self.print_at:
                self.get_logger().info(text)
                self.last_state = report['state']
                self.print_at = now + 1_000_000_000

    rclpy.init(args=args, signal_handler_options=signals.SignalHandlerOptions.ALL)
    node = None
    try:
        node = Monitor()
        rclpy.spin(node)
    except (KeyboardInterrupt, rclpy.executors.ExternalShutdownException):
        pass
    except rclpy_implementation.RCLError:
        if rclpy.ok():
            raise
    finally:
        if node is not None:
            node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
