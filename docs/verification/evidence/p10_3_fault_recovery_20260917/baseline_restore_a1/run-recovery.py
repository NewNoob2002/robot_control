"""Start independent capture and the single authorized target trial without chat scheduling gaps."""
from pathlib import Path
import json
import subprocess
import time
import sys
import signal
from operator_console import current_prompt

BASE = Path(__file__).resolve().parent
CAPTURE = BASE / 'recovery-jcan-once'
REMOTE = '/home/cat/.cache/robot-control/staging/p103-restore-4c7a757f-a1-20260917'
SHA = '4c7a757f578f774c0788221bd5d46da3247f53e9ca898001ef41b7f15472256f'


def interrupted(signum, _frame):
    """Route terminal closure and termination through the target abort/cleanup path."""
    raise RuntimeError('Operator terminal interrupted: signal ' + str(signum))


def main():
    """Consume one fresh capture and one target marker; never automatically relaunch either."""
    confirmation = json.loads((BASE / 'operator-confirmation.json').read_text())
    assert sys.stdout.isatty() and confirmation['operator_display_visible']
    assert confirmation['statement'] == '零目标基线恢复已上电就绪'
    assert confirmation['drive_power_on'] and confirmation['x1_released']
    assert confirmation['no_wheel_contact']
    assert confirmation['sticks_neutral'] and confirmation['button_released']
    assert confirmation['wheels_raised'] and confirmation['emergency_stop_available']
    assert 0 <= time.time()-confirmation['wall_time'] < 300
    signal.signal(signal.SIGHUP, interrupted)
    signal.signal(signal.SIGTERM, interrupted)
    observer = application = None
    result = {'passed': False, 'control_phase_observed': False}
    try:
        observer = subprocess.Popen([sys.executable, str(BASE / 'jcan-window.py')], start_new_session=True)
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
                                           stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            result['target_invoked'] = True
            until = time.monotonic() + 110
            capture_failed = False
            last_display = None
            last_size = 0
            last_progress = time.monotonic()
            while application.poll() is None:
                # Abort the target on observer failure; its wrapper signals the owner then permits cleanup.
                failed = observer.poll() is not None or (CAPTURE / 'result.json').exists()
                if failed and not capture_failed:
                    subprocess.run(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=3',
                                    'robot-dev', 'touch ' + REMOTE + '/abort'], check=True, timeout=5)
                capture_failed = capture_failed or failed
                contents = (BASE / 'recovery-execution.log').read_text()
                if application.poll() is not None:
                    break
                phase, message, remaining = current_prompt(contents)
                if len(contents) != last_size:
                    last_progress = time.monotonic()
                    last_size = len(contents)
                if phase != 'WAIT':
                    assert time.monotonic() - last_progress < 3, 'live target progress lost; aborting'
                display = (phase, remaining)
                if display != last_display:
                    if last_display is None or phase != last_display[0]:
                        print(f'\n[{phase}] {message}', flush=True)
                    print(f'\r[{phase}] 提示窗口剩余约{remaining if remaining is not None else "?"}秒    ', end='', flush=True)
                    result.setdefault('display_events', []).append(dict(phase=phase, remaining=remaining, host_monotonic_ns=time.monotonic_ns()))
                    result['last_display'] = phase
                    if phase == 'CONTROL_READY':
                        result['operator_prompted'] = True
                        result['control_phase_observed'] = True
                    last_display = display
                assert time.monotonic() < until, 'target runner timeout'
                time.sleep(0.05)
            result['target_runner_exit'] = application.returncode
            assert application.returncode == 0 and not capture_failed, 'target or independent capture failed'
        result['passed'] = True
    except BaseException as exc:
        result['error'] = str(exc)
        signal.signal(signal.SIGHUP, signal.SIG_IGN)
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        if application is not None and application.poll() is None:
            aborted = subprocess.run(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=3',
                                      'robot-dev', 'touch ' + REMOTE + '/abort'], timeout=5, check=False)
            result['abort_request_exit'] = aborted.returncode
        raise
    finally:
        # Keep the independent capture alive while the target handles abort and restores.
        if application is not None and application.poll() is None:
            try:
                application.wait(timeout=15)
            except subprocess.TimeoutExpired:
                result['target_cleanup_unconfirmed'] = True
                print('目标清理未确认：请立即关闭驱动电源。', flush=True)
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
