#!/usr/bin/env python3
"""Check explicit device selection and file logging on paths that cannot access robot hardware."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def main():
    """Run config-only and missing-interface cases; verify logger footer and diagnostic file creation."""
    binary, config = str(Path(sys.argv[1]).resolve()), str(Path(sys.argv[2]).resolve())
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        base = [binary, '--config', config, '--log-dir', str(root / 'logs')]
        checked = subprocess.run(base + ['--interface', 'can7', '--device', '/dev/ttyACM0', '--check-config'],
                                 capture_output=True, text=True, timeout=5)
        assert checked.returncode == 0, checked.stderr
        assert 'interface=can7' in checked.stdout and 'device=/dev/ttyACM0' in checked.stdout
        assert not (root / 'logs').exists()
        for args in (['--interface', 'can0', '--set', 'interface=can1'], ['--ipc'],
                     ['--set', 'control_source=ipc'], ['--duration-ms', '-1']):
            result = subprocess.run(base + args, capture_output=True, text=True, timeout=5)
            assert result.returncode == 2, result
        result = subprocess.run(base + ['--interface', 'no_such_can_rc', '--device', '/dev/no_such_uart_rc'],
                                capture_output=True, text=True, timeout=8)
        assert result.returncode == 1, result
        assert 'logs_complete=1' in result.stdout, result
        runs = list((root / 'logs').glob('run-*'))
        assert len(runs) == 1
        run = runs[0]
        assert 'startup_error:' in (run / 'application.log').read_text()
        assert 'logger_end' in (run / 'diagnostics.log').read_text()
        raw = (run / 'trace.bin').read_bytes()
        assert len(raw) == 256
        packet = struct.unpack('<32q', raw)
        assert packet[0] == 0x31414352 and packet[2] == 7 and packet[4] == 1
        assert (run.stat().st_mode & 0o777) == 0o700
    print('PASS: interface/device options, IPC rejection, no-device config check, per-run text/raw logs')


if __name__ == '__main__':
    main()
