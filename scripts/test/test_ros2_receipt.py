#!/usr/bin/env python3
"""Deterministic receipt freshness checks; no ROS, sleeps, sockets or remote clock arithmetic."""
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    'ipc_monitor', Path(__file__).resolve().parents[2] / 'ros2/robot_control_ipc_monitor.py')
assert spec is not None and spec.loader is not None
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def main():
    """Cover exact expiry, bad input, duplicates, new sessions and reconnect without automatic authority."""
    now = [0]
    state = module.ReceiptState(lambda: now[0])
    assert state.report()['state'] == 'WAITING'
    snapshot = {'version': 1, 'connected': True, 'executed': False, 'motion_authority': False,
                'server_started_ns': 999999999999999, 'session': 1, 'sequence': 1,
                'captured_ns': 999999999999999, 'cycles': 1, 'healthy': True}
    state.receive(json.dumps(snapshot))
    assert state.report()['state'] == 'FRESH' and state.report()['healthy']
    assert state.report()['source_age_ms'] is None and not state.report()['source_age_verified']
    now[0] = 499_999_999
    assert state.report()['state'] == 'FRESH'
    now[0] += 1
    assert state.report()['state'] == 'STALE' and not state.report()['healthy']
    assert state.report()['connected'] is None and state.report()['ipc_connected_reported']
    state.receive(json.dumps({'version': 1, 'connected': True, 'executed': False,
                             'motion_authority': False, 'probe_error': 'bad probe'}))
    assert state.report()['state'] == 'STALE' and state.report()['receipt_age_ms'] == 500
    state.receive(json.dumps(snapshot))
    assert state.report()['state'] == 'INVALID' and state.report()['receipt_age_ms'] == 500
    snapshot.update(session=2, captured_ns=1, sequence=1)
    state.receive(json.dumps(snapshot))
    assert state.report()['state'] == 'FRESH'
    state.receive(json.dumps({'version': 1, 'connected': False, 'executed': False,
                             'motion_authority': False, 'error': 'IPC closed'}))
    assert state.report()['state'] == 'DISCONNECTED' and not state.report()['healthy']
    assert state.report()['stale'] and state.report()['connected'] is False
    for invalid in (None, '[]', '{', '[' * 1800 + ']' * 1800, ' ' * 4097,
                    json.dumps({**snapshot, 'version': True}), json.dumps({**snapshot, 'motion_authority': 0}),
                    json.dumps({**snapshot, 'cycles': True}), json.dumps({**snapshot, 'healthy': 'true'})):
        state.receive(invalid)
        assert state.report()['state'] == 'INVALID' and not state.report()['healthy']
    snapshot.update(sequence=2, captured_ns=2)
    state.receive(json.dumps(snapshot))
    assert state.report()['state'] == 'FRESH'
    now[0] -= 1
    assert state.report()['state'] == 'STALE' and not state.report()['healthy']
    print('PASS: local receipt expiry, malformed/replayed revisions, reconnect and unknown source age')


if __name__ == '__main__':
    main()
