#!/usr/bin/env python3
"""ROS2 Humble read-only app IPC bridge; no device access or motion authorization."""
import json
import socket
import time


class Client:
    """Single-owner nonblocking client, one outstanding request, no retransmission of probes."""

    def __init__(self, path):
        """Retain an explicit local socket path; opening is deferred to tick."""
        if not path.startswith('/') or len(path.encode()) >= 108:
            raise ValueError('socket_path must be an absolute Unix socket path shorter than 108 bytes')
        self.path = path
        self.sock = None
        self.sequence = 0
        self.pending = None
        self.probe = None
        self.retry_at = 0

    def close(self):
        """Drop connection and unsent probes; reconnect never replays an old request."""
        if self.sock is not None:
            self.sock.close()
        self.sock = self.pending = self.probe = None
        self.retry_at = time.monotonic_ns() + 500_000_000

    def submit_probe(self, text):
        """Keep one validated probe with its original CLOCK_MONOTONIC acquisition time."""
        if len(text) > 256:
            raise ValueError('probe exceeds 256 characters')
        # on_probe catches invalid JSON as ValueError and reports a rejected probe.
        # pi-lens-ignore: unchecked-throwing-call-python
        value = json.loads(text)
        if not isinstance(value, dict) or set(value) != {'captured_ns', 'left_rpm', 'right_rpm'}:
            raise ValueError('probe requires captured_ns, left_rpm and right_rpm')
        if any(type(item) is not int for item in value.values()):
            raise ValueError('probe fields must be integers')
        age = time.monotonic_ns() - value['captured_ns']
        if not 0 <= age < 100_000_000 or any(abs(value[key]) > 1000 for key in ('left_rpm', 'right_rpm')):
            raise ValueError('stale/future timestamp or rpm outside protocol limit')
        if self.sock is None:
            raise ValueError('IPC is disconnected; probe discarded')
        self.probe = value

    def tick(self):
        """Make bounded progress; return a fresh server response or explicit disconnected status."""
        now = time.monotonic_ns()
        try:
            if self.sock is None:
                if now < self.retry_at:
                    return None
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET | socket.SOCK_NONBLOCK)
                self.sock.connect(self.path)
                self.sequence = 0
            if self.pending is not None:
                if now - self.pending >= 100_000_000:
                    raise TimeoutError('IPC response timeout')
                try:
                    raw, _, flags, _ = self.sock.recvmsg(1024)
                except BlockingIOError:
                    return None
                if not raw or flags & (socket.MSG_TRUNC | socket.MSG_CTRUNC):
                    raise ValueError('IPC closed or truncated response')
                reply = json.loads(raw)
                if (not isinstance(reply, dict) or reply.get('version') != 1
                        or reply.get('sequence') != self.sequence
                        # Exact booleans are required; integer zero must not satisfy this contract.
                        # pi-lens-ignore: no-identity-operator-on-literals
                        or reply.get('executed') is not False or reply.get('motion_authority') is not False
                        or type(reply.get('captured_ns')) is not int
                        or not 0 <= time.monotonic_ns() - reply['captured_ns'] < 100_000_000):
                    raise ValueError('invalid or stale IPC response')
                self.pending = None
                reply['connected'] = True
                return reply
            self.sequence += 1
            probe, self.probe = self.probe, None
            kind = 1 if probe is not None else 0
            stamp, left, right = ((probe['captured_ns'], probe['left_rpm'], probe['right_rpm'])
                                  if probe is not None else (now, 0, 0))
            packet = f'RCI1 {self.sequence} {stamp} {kind} {left} {right}'.encode('ascii')
            if self.sock.send(packet, socket.MSG_NOSIGNAL) != len(packet):
                raise OSError('incomplete IPC request')
            self.pending = now
        except (OSError, ValueError, UnicodeError) as error:
            self.close()
            return {'version': 1, 'connected': False, 'executed': False, 'motion_authority': False,
                    'error': str(error)}
        return None


def main(args=None):
    """Run the ROS adapter separately from the app; callbacks only exchange read-only IPC packets."""
    # ROS imports are verified in the pinned Humble test image, not installed in the host analyzer.
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

    class IpcNode(Node):
        """Expose diagnostic JSON and a latest-only validation probe; never accept cmd_vel."""

        def __init__(self):
            """Declare startup-only endpoint and volatile depth-one ROS interfaces."""
            super().__init__('robot_control_ipc')
            self.declare_parameter('socket_path', '')
            self.client = Client(self.get_parameter('socket_path').value)
            qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.BEST_EFFORT,
                             durability=DurabilityPolicy.VOLATILE)
            self.status = self.create_publisher(String, '~/status', qos)
            self.probes = self.create_subscription(String, '~/probe', self.on_probe, qos)
            # IPC deadlines and reconnects must progress while /clock is absent or paused.
            self.timer = self.create_timer(0.02, self.on_tick, clock=Clock(clock_type=ClockType.STEADY_TIME))

        def on_probe(self, message):
            """Reject malformed/stale probes without changing app authority or stopping the node."""
            try:
                self.client.submit_probe(message.data)
            except (ValueError, TypeError) as error:
                self.status.publish(String(data=json.dumps({'connected': self.client.sock is not None,
                    'executed': False, 'motion_authority': False, 'probe_error': str(error)})))

        def on_tick(self):
            """Publish only newly received observations or an explicit connection failure."""
            result = self.client.tick()
            if result is not None:
                self.status.publish(String(data=json.dumps(result)))

    # SIGTERM must also close IPC; the app has its own independent signal/stop owner.
    rclpy.init(args=args, signal_handler_options=signals.SignalHandlerOptions.ALL)
    node = None
    try:
        node = IpcNode()
        rclpy.spin(node)
    except (KeyboardInterrupt, rclpy.executors.ExternalShutdownException):
        pass
    except rclpy_implementation.RCLError:
        # Humble may invalidate the context between executor checks and wait-set creation.
        # A live-context ROS failure must still reach the caller.
        if rclpy.ok():
            raise
    finally:
        if node is not None:
            node.client.close()
            node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
