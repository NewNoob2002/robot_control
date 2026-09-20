"""Locally confirm and supervise one G5 ground turn-right motion trial with dual capture."""
import json
import signal
import re
import subprocess
import sys
import time
from pathlib import Path

BASE = Path(__file__).resolve().parent
REMOTE = '/home/cat/.cache/robot-control/staging/g3-differential-51f94c1f-20260920/ground_turn_right_a1'
SHA = '51f94c1f51945f3d28fa945d58f14cb15ad4ec7a4cab353331a2f39e7ca16451'


def interrupted(signum, _frame):
    """Revoke the target lease through the normal exception cleanup path."""
    raise RuntimeError('operator terminal interrupted: ' + str(signum))


def current_prompt(log):
    """Reject stale movement cues after stop, exit, or expiration."""
    ticks = re.findall(r'^WINDOW_REMAINING: (\d+)$', log, re.MULTILINE)
    if 'APPLICATION_STOPPING:' in log or 'APPLICATION_EXIT:' in log or (ticks and int(ticks[-1]) == 0):
        return 'STOP', '已进入停止：立即摇杆回中，不再给运动输入，等待清理，车辆静止并机械防溜后再关闭驱动电源。'
    if 'MOTION_READY:' in log:
        return 'MOTION_READY', '现在仅将转向向右推足并保持，油门始终中立；左轮前进、右轮后退，车头应右转。STOP或实际停轮立即回中。'
    if 'CONTROL_READY:' in log:
        return 'CONTROL_READY', '保持双摇杆中立，现在按下并释放CH6一次；等待MOTION_READY再操作摇杆。'
    return 'WAIT', '正在准备，保持中立，暂不操作CH6。'


def main():
    """Require local readiness, consume once, display live cues, and retain disposition."""
    assert sys.stdin.isatty() and sys.stdout.isatty()
    assert not (BASE / 'CONSUMED').exists()
    assert json.loads((BASE/'ground-plan.json').read_text())['ready_for_execution'], 'Ground travel/stop boundary is not yet established'
    print('G5 地面原地右转 A1：左轮前进、右轮后退，各轮目标绝对值不超过5rpm；3秒硬限/2950ms截止。')
    print('重新确认车身和突出部件完整转动范围内无人无障碍；周围净空与围挡适用于原地旋转。')
    print('完整车身旋转范围之外四周至少3米净空，围挡牢固适用；标记车体中心起点及中心偏移1米边界。')
    print('中心偏移达1米、转错方向、STOP后仍运动或异常立即X1；无效再切断动力，禁止用身体拦车。')
    print('中心偏移须小于1米，停止后中心位移不超过0.5米；记录转角、停止后余转及中心偏移，目测必须注明。')
    print('总准备窗口60秒；稳定近零反馈±2rpm，活动反馈绝对值硬限7.5rpm。')
    print('CONTROL_READY：中立按放CH6一次；等MOTION_READY只向右打转向，油门始终中立。')
    print('保持右转向直到STOP或实际停轮，立即回中；车头应向右转，停止后不再操作。')
    print('油门偏离中立可能触发模式保护；发生保护退出不继续尝试。')
    print('启停短暂反馈跳变按已确认规则留原始值复核；现有在线保护未修改，持续异常仍失败。')
    print('请实际确认车辆落地无载、无人触碰、驱动已供电、X1释放且操作者能立即按下；按现场停止方案处置，断动力会滑行；')
    print('接线未变、发射机和接收机在线、摇杆中立、CH6松开，能看清本窗口。')
    print('不主动拔线/关遥控器/按X1制造故障；实际方向错误、异常轮动或异响时立即X1；按现场停止方案处置，断动力会滑行。')
    if input('上述全部属实，理解原地右转动作并授权A1仅一次，输入 START；否则取消 > ').strip() != 'START':
        return 2
    confirmation = dict(wall_time=time.time(), response='START', drive_power_on=True,
                        ground_unloaded=True, swept_area_confirmed=True, containment_ready=True, intervention_line_marked=True, ground_plan_sha256='61ee718cc0d2cf6aa59f16dbefbc1aa71fc6c669395f32eaacc849a54e6d30ed', emergency_stop_available=True, x1_released=True,
                        sticks_neutral=True, ch6_released=True, transmitter_receiver_online=True,
                        unchanged_wiring=True, operator_display_visible=True)
    (BASE / 'operator-start.json').write_text(json.dumps(confirmation, indent=2))
    auth = json.loads((BASE / 'authorization.json').read_text())
    auth.update(authorized=True, authorization='User authorized G5 ground turn-right trial; fresh START confirms barrier, empty area and clear swept area and1m center-drift boundary; <=5rpm/<3s')
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
                phase, message = current_prompt(contents)
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
                        print('目标清理未确认：立即X1；若仍运动则切断动力，人员远离运动区域，围挡保持有效。', flush=True)
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
    print('\n试验已结束，保持中立，锁定X1，确认车辆静止并机械防溜后再关闭驱动电源。结果还需离线审查。', flush=True)
    print(json.dumps(result), flush=True)
    measurement = input('记录右转角度、停止后余转角/时间、车体中心偏移（注明尺量/录像/目测；未测写未测），以及有无X1介入、围挡接触或中心越过1米线 > ').strip()
    (BASE/'ground-observations.json').write_text(json.dumps(dict(raw=measurement, source='local operator measurement'),ensure_ascii=False,indent=2))
    answer = input('若车头右转、左轮前进右轮后退且停止正常、无异响、驱动已断电，输入 OFF；否则描述实际情况 > ').strip()
    (BASE / 'operator-off.json').write_text(json.dumps(dict(wall_time=time.time(), response=answer,
        left_forward_and_stop_normal=True if answer == 'OFF' else None, right_backward_and_stop_normal=True if answer == 'OFF' else None, no_abnormal_sound=True if answer == 'OFF' else None,
        drive_power_off=True if answer == 'OFF' else None), ensure_ascii=False, indent=2))
    print('现场反馈已保存，可关闭本窗口。', flush=True)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
