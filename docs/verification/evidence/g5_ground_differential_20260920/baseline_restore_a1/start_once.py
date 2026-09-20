"""Locally confirm and supervise one G3 zero-x1-recovery motion trial with dual capture."""
import json
import signal
import re
import subprocess
import sys
import time
from pathlib import Path

BASE = Path(__file__).resolve().parent
REMOTE = '/home/cat/.cache/robot-control/staging/g3-differential-51f94c1f-20260920/ground_baseline_restore_a1'
SHA = '51f94c1f51945f3d28fa945d58f14cb15ad4ec7a4cab353331a2f39e7ca16451'


def interrupted(signum, _frame):
    """Revoke the target lease through the normal exception cleanup path."""
    raise RuntimeError('operator terminal interrupted: ' + str(signum))


PROMPTS = (('RESTORE_ACTIVE', '正在核对并恢复已知残留基线；保持静止，不操作CH6/X1或摇杆。'),)


def current_prompt(log):
    """Return only the latest live phase; an exit or expired window cancels all actions."""
    ticks = re.findall(r'^WINDOW_REMAINING: (\d+)$', log, re.MULTILINE)
    remaining = int(ticks[-1]) if ticks else None
    if 'APPLICATION_EXIT:' in log or 'APPLICATION_STOPPING:' in log or remaining == 0:
        return 'STOP', '应用已停止或窗口已结束，不再执行阶段动作；摇杆回中，等待清理并确认车辆机械防溜后关闭驱动电源。', remaining
    phases = [(log.rfind(name + ':'), name, message) for name, message in PROMPTS]
    offset, name, message = max(phases)
    return (name, message, remaining) if offset >= 0 else ('WAIT', '正在准备采集和应用；保持中立，暂不操作 CH6 或 X1。', remaining)


def main():
    """Require local readiness, consume once, display live cues, and retain disposition."""
    assert sys.stdin.isatty() and sys.stdout.isatty()
    assert not (BASE / 'CONSUMED').exists()
    print('地面基线恢复 A1：仅回读及恢复已知易失参数，全程禁能、零目标，不进入运动控制。')
    print('确认车辆机械防溜有效、全程无人推车或触碰车轮；驱动重新上电，X1已释放且可立即急停。')
    print('接线未变，遥控器/接收机在线，摇杆中立，CH6松开；本项不操作任何遥控通道或X1。')
    print('已知残留状态不匹配会拒绝写入，不自动切换测试；结束后先停稳防溜再断动力。')
    if input('确认以上条件并执行已授权基线恢复一次，输入 START；否则取消 > ').strip() != 'START':
        return 2
    confirmation = dict(wall_time=time.time(), response='START', drive_power_on=True,
                        ground_stationary=True, mechanical_restraint=True, emergency_stop_available=True, x1_released=True,
                        sticks_neutral=True, ch6_released=True, transmitter_receiver_online=True,
                        unchanged_wiring=True, operator_display_visible=True)
    (BASE / 'operator-start.json').write_text(json.dumps(confirmation, indent=2))
    auth = json.loads((BASE / 'authorization.json').read_text())
    auth.update(authorized=True, authorization='User authorized baseline recovery before retry; fresh local START confirms powered stationary restrained readiness')
    (BASE / 'authorization.json').write_text(json.dumps(auth, indent=2))
    with (BASE / 'CONSUMED').open('x') as marker:
        marker.write(str(time.time()))
    for sig in (signal.SIGHUP, signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, interrupted)
    capture = target = None
    result = dict(passed=False, target_invoked=False)
    with (BASE / 'capture-supervisor.log').open('wb') as caplog, (BASE / 'target-stream.log').open('wb') as log:
        try:
            capture = subprocess.Popen([sys.executable, '-B', str(BASE / 'jcan/capture_once.py'), '--run'],
                                       stdout=caplog, stderr=subprocess.STDOUT, start_new_session=True)
            until = time.monotonic() + 20
            while not (BASE / 'jcan/progress.json').exists():
                assert capture.poll() is None and time.monotonic() < until, 'capture startup failed'
                time.sleep(0.1)
            assert capture.poll() is None and not (BASE / 'jcan/result.json').exists()
            ready = dict(confirmation, independent_capture_ready=True, artifact_sha256=SHA)
            ready['wall_time'] = time.time()
            (BASE / 'operator-ready.json').write_text(json.dumps(ready, indent=2))
            subprocess.run(['scp', '-q', str(BASE / 'operator-ready.json'), str(BASE / 'authorization.json'),
                            'robot-dev:' + REMOTE + '/'], check=True, timeout=8)
            assert capture.poll() is None and not (BASE / 'jcan/result.json').exists()
            target = subprocess.Popen(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5',
                                       'robot-dev', 'python3 -B -u ' + REMOTE + '/target_once.py'],
                                      stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            result['target_invoked'] = True
            until = time.monotonic() + 110
            next_lease = 0
            last_size = 0
            last_progress = time.monotonic()
            while target.poll() is None:
                assert capture.poll() is None and not (BASE / 'jcan/result.json').exists(), 'capture failed'
                assert time.time() - (BASE / 'jcan/progress.json').stat().st_mtime < 15, 'capture status stale'
                assert time.monotonic() < until, 'target exceeded deadline'
                contents = (BASE / 'target-stream.log').read_text()
                if len(contents) != last_size:
                    last_progress, last_size = time.monotonic(), len(contents)
                assert time.monotonic() - last_progress < 8, 'target progress lost'
                phase, message, _ = current_prompt(contents)
                if result.get('display_phase') != phase:
                    print('\n'+phase+'：'+message, flush=True)
                    result['display_phase'] = phase
                    result.setdefault('display_events', []).append(dict(phase=phase, monotonic_ns=time.monotonic_ns()))
                if time.monotonic() >= next_lease:
                    target.stdin.write(b'LEASE\n')
                    target.stdin.flush()
                    next_lease = time.monotonic() + 0.5
                time.sleep(0.05)
            result['target_exit'] = target.returncode
            assert target.returncode == 0, 'target failed; no retry'
            result['passed'] = True
        except BaseException as exc:
            result['error'] = str(exc)
        finally:
            for sig in (signal.SIGHUP, signal.SIGTERM, signal.SIGINT):
                signal.signal(sig, signal.SIG_IGN)
            if target is not None:
                target.stdin.close()
                if target.poll() is None:
                    try:
                        target.wait(timeout=20)
                    except subprocess.TimeoutExpired:
                        result['target_cleanup_unconfirmed'] = True
                        result['passed'] = False
                        print('目标清理未确认：立即X1；机械防溜必须保持，必要时断动力。', flush=True)
            (BASE / 'jcan/STOP').touch()
            if capture is not None and capture.poll() is None:
                try:
                    capture.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    capture.terminate()
                    result['capture_cleanup_unconfirmed'] = True
                    result['passed'] = False
            if (BASE / 'jcan/result.json').exists():
                captured = json.loads((BASE / 'jcan/result.json').read_text())
                result['capture_clean'] = not captured['error'] and captured['shutdown_ack'] and captured['process_rc'] == 0
                result['passed'] &= result['capture_clean']
            else:
                result['passed'] = False
            (BASE / 'result.json').write_text(json.dumps(result, indent=2))
    print('\n试验已结束，保持中立，锁定X1，确认车辆静止且机械防溜有效后再关闭驱动电源。结果还需离线审查。', flush=True)
    print(json.dumps(result), flush=True)
    answer = input('若车辆全程静止、无异响、防溜仍有效且驱动已断电，输入 OFF；否则描述实际情况 > ').strip()
    (BASE / 'operator-off.json').write_text(json.dumps(dict(wall_time=time.time(), response=answer,
        vehicle_stationary=True if answer == 'OFF' else None, mechanical_restraint_retained=True if answer == 'OFF' else None, no_abnormal_sound=True if answer == 'OFF' else None,
        drive_power_off=True if answer == 'OFF' else None), ensure_ascii=False, indent=2))
    print('现场反馈已保存，可关闭本窗口。', flush=True)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
