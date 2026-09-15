#!/usr/bin/env python3
"""Restore only can0's administrative state once, with the drive powered off."""
import json
import os
import subprocess
from pathlib import Path

BASE = Path(__file__).resolve().parent

def snapshot():
    """Read physical interface state without transmitting application frames."""
    return json.loads(subprocess.check_output(
        ['ip', '-j', '-details', '-statistics', 'link', 'show', 'can0'], text=True, timeout=5))

def main():
    """Require local power confirmation and preserve before/after evidence."""
    assert os.geteuid() == 0, 'Run with sudo in the RK3588 terminal'
    assert Path('/etc/machine-id').read_text().strip() == '6923ab3301fb4a8d816759b04ec6bf0a'
    assert input('Keep drive POWER OFF. Type POWER_OFF to restore can0 once: ').strip() == 'POWER_OFF'
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit():
            continue
        try:
            name = (entry / 'exe').resolve(strict=True).name
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            continue
        assert name not in ('cansend', 'cangen', 'robot-control-zlac-qualification',
                            'robot-control-canopen-commission'), 'Active CAN sender: ' + name
    pre = snapshot()
    assert pre[0]['linkinfo']['info_data']['bittiming']['bitrate'] == 500000
    assert pre[0]['mtu'] == 16
    out = BASE / 'interface_restore_once'
    out.mkdir(exist_ok=False)
    (out / 'before.json').write_text(json.dumps(pre, indent=2))
    (out / 'operator_confirmation.txt').write_text('POWER_OFF\n')
    try:
        subprocess.run(['ip', 'link', 'set', 'dev', 'can0', 'down'], check=True, timeout=5)
        subprocess.run(['ip', 'link', 'set', 'dev', 'can0', 'up'], check=True, timeout=5)
        post = snapshot()
        (out / 'after.json').write_text(json.dumps(post, indent=2))
        info = post[0]['linkinfo']['info_data']
        assert 'UP' in post[0]['flags'] and info['state'] == 'ERROR-ACTIVE', post
        assert info['bittiming']['bitrate'] == 500000
        assert info['restart_ms'] == pre[0]['linkinfo']['info_data']['restart_ms']
        assert all(value == 0 for value in info.get('berr_counter', {}).values())
        (out / 'result.json').write_text(json.dumps({'pass': True, 'drive_power': 'operator_confirmed_off', 'application_tx': False}))
    except BaseException:
        subprocess.run(['ip', 'link', 'set', 'dev', 'can0', 'down'], timeout=5, check=False)
        (out / 'result.json').write_text(json.dumps({'pass': False, 'retry_authorized': False}))
        raise
    print('DONE: can0 ERROR-ACTIVE. Keep drive POWER OFF; no motor test started.', flush=True)

if __name__ == '__main__':
    main()
