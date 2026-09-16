"""Start independent capture and the single authorized target trial without chat scheduling gaps."""
from pathlib import Path
import json
import subprocess
import time
import sys

BASE = Path(__file__).resolve().parent
CAPTURE = BASE / 'diagnostics-jcan-attempt2'
REMOTE = '/home/cat/.cache/robot-control/staging/p103-diag-757c232d-20260916'
SHA = '757c232db0a391f75fdf3d84fb67290721bfb8b3d8e00a3d723316639c981f5a'


def main():
    """Consume one fresh capture and one target marker; never automatically relaunch either."""
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
                      'operator_statement': '已上电就绪',
                      'readiness_scope': 'Unchanged operator-confirmed fixture; registered when independent capture ready'})
        ready_path = BASE / 'diagnostics-operator-ready.json'
        ready_path.write_text(json.dumps(ready, indent=2) + chr(10))
        subprocess.run(['scp', '-q', str(ready_path), 'robot-dev:' + REMOTE + '/operator-ready.json'],
                       check=True, timeout=8)
        assert observer.poll() is None and not (CAPTURE / 'result.json').exists()
        with (BASE / 'diagnostics-execution.log').open('wb') as log:
            application = subprocess.Popen(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5',
                                            'robot-dev', 'python3 ' + REMOTE + '/diagnostics-once.py'],
                                           stdout=log, stderr=subprocess.STDOUT)
            result['physical_started'] = True
            until = time.monotonic() + 30
            capture_failed = False
            while application.poll() is None:
                # This no-motion tool has its own 20s bound and restoration; let it finish cleanup.
                capture_failed = capture_failed or observer.poll() is not None or (CAPTURE / 'result.json').exists()
                assert time.monotonic() < until, 'target runner timeout'
                time.sleep(0.05)
            result['target_runner_exit'] = application.returncode
            assert application.returncode == 0 and not capture_failed, 'target or independent capture failed'
        result['passed'] = True
    except BaseException as exc:
        result['error'] = str(exc)
        raise
    finally:
        if CAPTURE.is_dir():
            (CAPTURE / 'stop').touch()
        if observer is not None:
            observer.wait(timeout=7)
            result['capture_exit'] = observer.returncode
            result['passed'] = result['passed'] and observer.returncode == 0
        (BASE / 'diagnostics-orchestration.json').write_text(json.dumps(result, indent=2) + chr(10))
        print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
