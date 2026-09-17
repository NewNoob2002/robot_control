"""Start independent capture and the single authorized target trial without chat scheduling gaps."""
from pathlib import Path
import json
import subprocess
import time
import sys

BASE = Path(__file__).resolve().parent
CAPTURE = BASE / 'recovery-jcan-once'
REMOTE = '/home/cat/.cache/robot-control/staging/p103-faults-2f97bdba-x1-a1-20260917'
SHA = '2f97bdbafdb370e70ae47231609cf1ee698e56af2652f6e12e6c041970389058'


def main():
    """Consume one fresh capture and one target marker; never automatically relaunch either."""
    confirmation = json.loads((BASE / 'operator-confirmation.json').read_text())
    assert confirmation['statement'] == 'F1零目标X1恢复已上电就绪'
    assert confirmation['drive_power_on'] and confirmation['x1_released']
    assert confirmation['no_wheel_contact']
    assert confirmation['sticks_neutral'] and confirmation['button_released']
    assert confirmation['wheels_raised'] and confirmation['emergency_stop_available']
    assert 0 <= time.time()-confirmation['wall_time'] < 300
    observer = application = None
    result = {'passed': False, 'control_phase_observed': False}
    try:
        observer = subprocess.Popen([sys.executable, str(BASE / 'jcan-window.py')])
        until = time.monotonic() + 65
        while not (CAPTURE / 'ready.json').exists():
            assert observer.poll() is None and time.monotonic() < until, 'capture did not become ready'
            time.sleep(0.05)
        assert observer.poll() is None and not (CAPTURE / 'result.json').exists()
        ready = json.loads((CAPTURE / 'ready.json').read_text())
        assert 0 <= time.time() - ready['wall_time'] < 3
        ready.update({'artifact_sha256': SHA, 'drive_power_on': True, 'wheels_raised': True,
                      'emergency_stop_available': True, 'independent_capture_ready': True,
                      'operator_statement': confirmation['statement'],
                      'readiness_scope': 'Unchanged operator-confirmed fixture; registered when independent capture ready'})
        ready_path = BASE / 'recovery-operator-ready.json'
        ready_path.write_text(json.dumps(ready, indent=2) + chr(10))
        subprocess.run(['scp', '-q', str(ready_path), 'robot-dev:' + REMOTE + '/operator-ready.json'],
                       check=True, timeout=8)
        assert observer.poll() is None and not (CAPTURE / 'result.json').exists()
        with (BASE / 'recovery-execution.log').open('wb') as log:
            application = subprocess.Popen(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5',
                                            'robot-dev', 'python3 ' + REMOTE + '/recovery-once.py'],
                                           stdout=log, stderr=subprocess.STDOUT)
            result['target_invoked'] = True
            until = time.monotonic() + 110
            capture_failed = False
            while application.poll() is None:
                # Abort the target on observer failure; its wrapper signals the owner then permits cleanup.
                failed = observer.poll() is not None or (CAPTURE / 'result.json').exists()
                if failed and not capture_failed:
                    subprocess.run(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=3',
                                    'robot-dev', 'touch ' + REMOTE + '/abort'], check=True, timeout=5)
                capture_failed = capture_failed or failed
                if not result.get('operator_prompted') and 'CONTROL_READY:' in (BASE/'recovery-execution.log').read_text():
                    result['operator_prompted'] = True
                    result['control_phase_observed'] = True
                    print('CONTROL_READY: 保持摇杆中位，现在按CH6一次后松开；本次仅零目标恢复，等待FAULT_READY；总窗口60秒。', flush=True)
                for phase, prompt in (
                    ('FAULT_READY:', 'FAULT_READY：现在按锁X1急停，保持，等待下一提示。'),
                    ('RELEASE_FAULT:', 'RELEASE_FAULT：解除X1；油门向前保持，CH6按一次释放；等待NEUTRAL_READY。最终目标被独立限制为零。'),
                    ('NEUTRAL_READY:', 'NEUTRAL_READY：现在摇杆回中，CH6保持释放，等待REARM_READY。'),
                    ('REARM_READY:', 'REARM_READY：保持中立，CH6再按一次后释放。'),
                    ('ZERO_REENABLED:', 'ZERO_REENABLED：已重新零目标使能，保持中立，等待清理。'),
                    ('RECOVERY_COMPLETE:', 'RECOVERY_COMPLETE：恢复观察完成，保持中立，等待应用退出。')):
                    if not result.get(phase) and phase in (BASE/'recovery-execution.log').read_text():
                        result[phase] = True
                        print(prompt, flush=True)
                assert time.monotonic() < until, 'target runner timeout'
                time.sleep(0.05)
            result['target_runner_exit'] = application.returncode
            assert application.returncode == 0 and not capture_failed, 'target or independent capture failed'
        result['passed'] = True
    except BaseException as exc:
        result['error'] = str(exc)
        if application is not None and application.poll() is None:
            aborted = subprocess.run(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=3',
                                      'robot-dev', 'touch ' + REMOTE + '/abort'], timeout=5, check=False)
            result['abort_request_exit'] = aborted.returncode
        raise
    finally:
        if CAPTURE.is_dir():
            (CAPTURE / 'stop').touch()
        if observer is not None:
            observer.wait(timeout=7)
            result['capture_exit'] = observer.returncode
            result['passed'] = result['passed'] and observer.returncode == 0
        (BASE / 'recovery-orchestration.json').write_text(json.dumps(result, indent=2) + chr(10))
        print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
