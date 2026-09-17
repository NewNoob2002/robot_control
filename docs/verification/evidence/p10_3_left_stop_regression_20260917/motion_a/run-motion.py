"""Start independent capture and the single authorized target trial without chat scheduling gaps."""
from pathlib import Path
import json
import subprocess
import time
import sys

BASE = Path(__file__).resolve().parent
CAPTURE = BASE / 'motion-jcan-once'
REMOTE = '/home/cat/.cache/robot-control/staging/p103-left-held-9ac673e9-a1-20260917'
SHA = '9ac673e9b7b039467e346ac6595347c0da5e03cc37a01b8e90b1773df43fa7f2'


def main():
    """Consume one fresh capture and one target marker; never automatically relaunch either."""
    confirmation = json.loads((BASE / 'operator-confirmation.json').read_text())
    assert confirmation['statement'] == 'A工况已上电就绪'
    assert confirmation['drive_power_on']
    assert confirmation['no_wheel_contact']
    assert confirmation['sticks_neutral'] and confirmation['button_released']
    assert confirmation['wheels_raised'] and confirmation['emergency_stop_available']
    assert 0 <= time.time()-confirmation['wall_time'] < 300
    observer = application = None
    result = {'passed': False, 'physical_started': False}
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
        ready_path = BASE / 'motion-operator-ready.json'
        ready_path.write_text(json.dumps(ready, indent=2) + chr(10))
        subprocess.run(['scp', '-q', str(ready_path), 'robot-dev:' + REMOTE + '/operator-ready.json'],
                       check=True, timeout=8)
        assert observer.poll() is None and not (CAPTURE / 'result.json').exists()
        with (BASE / 'motion-execution.log').open('wb') as log:
            application = subprocess.Popen(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5',
                                            'robot-dev', 'python3 ' + REMOTE + '/motion-once.py'],
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
                if not result.get('operator_prompted') and 'CONTROL_READY:' in (BASE/'motion-execution.log').read_text():
                    result['operator_prompted'] = True
                    result['physical_started'] = True
                    print('CONTROL_READY: 保持摇杆中位，现在按CH6一次后松开；保持中位等待MOTION_READY；总窗口60秒。', flush=True)
                if not result.get('motion_prompted') and 'MOTION_READY:' in (BASE/'motion-execution.log').read_text():
                    result['motion_prompted'] = True
                    print('MOTION_READY: 左轮单轮；油门向前、转向向右同步推动，保持位置直到STOP_VERIFIED再回中；最多5rpm/3秒。', flush=True)
                if not result.get('stop_prompted') and 'STOP_VERIFIED:' in (BASE/'motion-execution.log').read_text():
                    result['stop_prompted'] = True
                    print('STOP_VERIFIED: 已观察到稳定零速，现在回中；勿触碰车轮，等待清理完成。', flush=True)
                if not result.get('unsafe_stop_prompted') and 'STOP_NOT_VERIFIED:' in (BASE/'motion-execution.log').read_text():
                    result['unsafe_stop_prompted'] = True
                    print('STOP_NOT_VERIFIED: 停止未核实，请立即急停/断电。', flush=True)
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
        (BASE / 'motion-orchestration.json').write_text(json.dumps(result, indent=2) + chr(10))
        print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
