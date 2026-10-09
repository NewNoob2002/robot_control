#!/usr/bin/env python3
"""Exercise the device-free startup CLI, strict parsing and effective profile round trip."""
import pathlib
import subprocess
import sys
import tempfile


def main():
    """Reject malformed configuration and verify overrides without touching device paths."""
    binary = str(pathlib.Path(sys.argv[1]).resolve())
    original = pathlib.Path(sys.argv[2]).read_text()
    with tempfile.TemporaryDirectory() as folder:
        root = pathlib.Path(folder)
        config = root / 'test.conf'

        def run(text, overrides=(), expected=0):
            """Run only the configuration checker; all writes stay in the private temporary directory."""
            config.write_text(text)
            result = subprocess.run([binary, '--config', str(config), *overrides],
                                    text=True, capture_output=True, timeout=5, cwd=root)
            assert result.returncode == expected, (result.returncode, result.stdout, result.stderr)
            return result.stdout

        normal = run(original)
        assert 'ipc_socket=off' in normal
        assert 'ipc_socket=/tmp/private/control.sock' in run(original, ['--set', 'ipc_socket=/tmp/private/control.sock'])
        assert 'CONFIG_VALID_DEVICE_ACCESS_NONE' in normal
        assert 'left=low/+1 right=high/-1' in normal
        assert 'standstill_tenths_rpm=20' in normal
        assert 'max_rpm=5' in normal
        for maximum, expected_gears in ((1,'1,1,1'), (2,'1,1,2'), (3,'1,1,3'), (5,'1,3,5'), (25,'7,15,25'), (100,'30,60,100')):
            configured = run(original, ['--set', f'max_rpm={maximum}'])
            assert f'gear_rpm={expected_gears}' in configured
            assert f'gear_full_throttle_rpm={expected_gears}' in configured
            assert 'output_deadband_rpm=1;' in configured

        assert 'max_rpm=25' in run(original, ['--set', 'max_rpm=25'])
        assert 'max_rpm=100' in run(original, ['--set', 'max_rpm=100'])
        assert 'speed_resolution=1' in normal
        for resolution in range(1, 11):
            assert f'speed_resolution={resolution}' in run(original, ['--set', f'speed_resolution={resolution}'])
        for resolution in ('0', '11', '-1', '0.1'):
            run(original, ['--set', f'speed_resolution={resolution}'], expected=2)
        for key, value in (('drive_max_rpm','100'), ('drive_accel_left_ms','800'),
                           ('drive_decel_right_ms','1000'), ('drive_quick_decel_left_ms','50'),
                           ('drive_io_stop','514'), ('drive_start_lock','1'),
                           ('drive_parking','1'), ('drive_direction','0')):
            assert f'{key}={value}' in run(original, ['--set', f'{key}={value}'])
        for key, value in (('drive_max_rpm','0'), ('drive_max_rpm','1001'),
                           ('drive_accel_left_ms','32768'), ('drive_decel_right_ms','-1'),
                           ('drive_io_stop','3'), ('drive_io_stop','259'), ('drive_direction','2'),
                           ('drive_mode','1'), ('drive_sync','0'), ('drive_quick_stop','7'),
                           ('drive_pdo_profile','2'), ('speed_resolution','keep')):
            run(original, ['--set', f'{key}={value}'], expected=2)
        run(original, ['--set','drive_max_rpm=4'], expected=2)
        assert 'drive_max_rpm=keep' in run(original, ['--set','drive_max_rpm=keep'])
        assert run(normal) == normal
        updated = run(original, ['--set', 'max_rpm=3',
                                 '--set', 'device=/dev/does-not-exist'])
        assert 'max_rpm=3' in updated
        assert 'device=/dev/does-not-exist' in updated
        assert sorted(p.name for p in root.iterdir()) == ['test.conf']
        run(original.replace('period_ms=10', 'period_ms=1'))
        run(original.replace('period_ms=10', 'period_ms=20'))
        run(original.replace('run_duration_ms=0', 'run_duration_ms=3600000'))
        cases = [
            '', original + 'max_rpm=4\n', original + 'typo=1\n',
            original.replace('max_rpm=5', 'max_rpm=0'),
            original.replace('max_rpm=5', 'max_rpm=101'),
            original.replace('max_rpm=5', 'max_rpm=-1'),
            original.replace('max_rpm=5', 'max_rpm=5rpm'),
            original.replace('max_rpm=5', 'max_rpm=9999999999999999999'),
            original.replace('max_rpm=5', 'max_rpm='),
            original.replace('period_ms=10', 'period_ms=50'),
            original.replace('period_ms=10', 'period_ms=0'),
            original.replace('log_files=4', 'log_files=0'),
            original.replace('log_file_bytes=67108864', 'log_file_bytes=1'),
            original.replace('run_duration_ms=0', 'run_duration_ms=-1'),
            original.replace('rk3588-zlac8015d-v1', 'unknown'),
            original.replace('interface=can0', 'interface=a' * 20),
            original.replace('interface=can0', 'interface=can/0'),
            original + 'control_source=ipc\n',
            original.replace('/dev/ttyACM0', '/tmp/input'),
            original.replace('/dev/ttyACM0', '/dev/../tmp/input'),
            original + '\x00=1\n', original + 'x' * 1025,
            original + '# comment\n' * 2000,
            original.replace('log_dir=./logs', 'log_dir=$HOME/logs'),
            original + 'ipc_socket=relative.sock\n',
            original + 'ipc_socket=/tmp/../control.sock\n',
            original + 'ipc_socket=/' + 'a' * 108 + '\n',
        ]
        for case in cases:
            run(case, expected=2)
        run(original, ['--set', 'max_rpm=3', '--set', 'max_rpm=4'], expected=2)
        run(original, ['--set', 'unknown=1'], expected=2)
        run(original, ['--set', 'max_rpm=4', '--unknown', '5'], expected=2)
        run(original, ['--set'], expected=2)
        run(original.replace('\n', '\r\n'))
        run(original.rstrip())
        run(original.replace('max_rpm=5', '  max_rpm = 5  '))
        assert sorted(p.name for p in root.iterdir()) == ['test.conf']
    print('PASS: strict parsing, range checks, CLI overrides, profile round trip; no device/log/socket access')


if __name__ == '__main__':
    main()
