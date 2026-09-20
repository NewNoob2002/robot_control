"""Locally confirm and supervise one G5 ground dual-positive motion trial with dual capture."""
import json
import signal
import re
import subprocess
import sys
import time
from pathlib import Path

BASE = Path(__file__).resolve().parent
REMOTE = '/home/cat/.cache/robot-control/staging/g3-differential-51f94c1f-20260920/ground_forward_a1'
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
        return 'MOTION_READY', '现在只将油门向前推足并保持，转向保持中立；两轮都应按车体前进方向转动。3秒内自动停止后按STOP提示回中。'
    if 'CONTROL_READY:' in log:
        return 'CONTROL_READY', '保持双摇杆中立，现在按下并释放CH6一次；等待MOTION_READY再操作摇杆。'
    return 'WAIT', '正在准备，保持中立，暂不操作CH6。'


def main():
    """Require local readiness, consume once, display live cues, and retain disposition."""
    assert sys.stdin.isatty() and sys.stdout.isatty()
    assert not (BASE / 'CONSUMED').exists()
    assert json.loads((BASE/'ground-plan.json').read_text())['ready_for_execution'], 'Ground travel/stop boundary is not yet established'
    print('G5 首次地面前进 A1（右轮极性−1）：车辆落地无载，每轮正目标相等、1..5rpm，单次非零硬限3秒/2950ms截止。')
    print('前方至少3米净空、围挡牢固且能阻挡本车，区域内无人；标记起点及前方1米干预线。')
    print('到1米线、STOP后仍行驶或异常，立即X1；若无效切断数字电源，围挡仅作最后防护，禁止用身体拦车。')
    print('本轮总位移须小于1米，停止后位移不超过0.5米；用固定机位录像并记录实测值，未测不能算停车距离通过。')
    print('总准备窗口60秒；稳定近零反馈±2rpm，活动反馈绝对值硬限7.5rpm。')
    print('CONTROL_READY：中立按下并释放CH6一次；等MOTION_READY再只推油门向前，转向始终中立。')
    print('保持前油门直到STOP或实际停轮，立即回中；两轮均应按车体前进方向转动，不以外侧观察旋转方向相同为准。')
    print('转向偏离中立可能导致两轮目标不等并保护退出；停止后不继续尝试。')
    print('启停短暂反馈跳变按已确认规则留原始值复核；现有在线保护未修改，持续异常仍失败。')
    print('请实际确认车辆落地无载、无人触碰、驱动已供电、X1释放且操作者能立即按下；按现场停止方案处置，断动力会滑行；')
    print('接线未变、发射机和接收机在线、摇杆中立、CH6松开，能看清本窗口。')
    print('不主动拔线/关遥控器/按X1制造故障；实际方向错误、异常轮动或异响时立即X1；按现场停止方案处置，断动力会滑行。')
    if input('上述全部属实，理解双轮前进动作并授权A1仅一次，输入 START；否则取消 > ').strip() != 'START':
        return 2
    confirmation = dict(wall_time=time.time(), response='START', drive_power_on=True,
                        ground_unloaded=True, containment_ready=True, intervention_line_marked=True, ground_plan_sha256='80ce0f14409d215635e5de4f838589b4967c484abe1f4fdda8ab6da134f58603', emergency_stop_available=True, x1_released=True,
                        sticks_neutral=True, ch6_released=True, transmitter_receiver_online=True,
                        unchanged_wiring=True, operator_display_visible=True)
    (BASE / 'operator-start.json').write_text(json.dumps(confirmation, indent=2))
    auth = json.loads((BASE / 'authorization.json').read_text())
    auth.update(authorized=True, authorization='User authorized first G5 ground forward trial; fresh START confirms barrier, empty area and marked1m intervention line; <=5rpm/<3s')
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
    measurement = input('记录起点至最终静止的实际距离、停止指令至静止的距离/时间（注明测量方式；未测写未测），以及有无X1介入、围挡接触或越过1米线 > ').strip()
    (BASE/'ground-observations.json').write_text(json.dumps(dict(raw=measurement, source='local operator measurement'),ensure_ascii=False,indent=2))
    answer = input('若左右两轮均为车体前进方向且停止正常、无异响、驱动已断电，输入 OFF；否则描述实际情况 > ').strip()
    (BASE / 'operator-off.json').write_text(json.dumps(dict(wall_time=time.time(), response=answer,
        left_forward_and_stop_normal=True if answer == 'OFF' else None, right_forward_and_stop_normal=True if answer == 'OFF' else None, no_abnormal_sound=True if answer == 'OFF' else None,
        drive_power_off=True if answer == 'OFF' else None), ensure_ascii=False, indent=2))
    print('现场反馈已保存，可关闭本窗口。', flush=True)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
