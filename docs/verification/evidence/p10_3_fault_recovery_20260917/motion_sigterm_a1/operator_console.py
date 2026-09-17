"""Direct, local operator instructions for one right-wheel moving SIGTERM trial."""
from pathlib import Path
import json
import re
import subprocess
import sys
import time

BASE = Path(__file__).resolve().parent
PROMPTS = (
    ('INPUT_WAIT', '等待遥控输入：遥控器可能未开机、未连接或正在恢复。请开启手持遥控器；接收机保持供电，摇杆回中、CH6 松开，等待连接提示。总就绪窗口60秒，不会自动延长。'),
    ('INPUT_NEUTRAL', '已收到健康输入，但摇杆未回中；请将油门和转向回中，不按 CH6。'),
    ('INPUT_RELEASE', '输入已恢复且摇杆中立；请先松开 CH6，等待允许授权提示。'),
    ('CONTROL_READY', '摇杆中立，CH6 按一次后松开，等待运动提示。'),
    ('ARMING', '已收到本次中立 CH6 授权，正在零目标使能；保持中立，等待 MOTION_READY。'),
    ('MOTION_READY', '只向前推油门、转向保持中立；保持油门，执行器会在观察到右轮正反馈约300ms后自动发送 SIGTERM。看到 SIGNAL_SENT 或停止提示立即回中。不要关闭遥控器、断开信号或按 X1；异常时立即急停/断电。'),
    ('SIGNAL_SENT', '已向核验的应用子进程发送 SIGTERM：摇杆回中，不再按 CH6，等待停车确认和清理。'),
    ('STOP_VERIFIED', '已观察到停车；摇杆保持中立、遥控器和接收机保持开启，等待清理后关闭驱动电源。'),
    ('STOP_NOT_VERIFIED', '停车未确认，立即现场急停、关闭驱动电源。'),
)


def current_prompt(log):
    """Return only the latest live phase; an exit or expired window cancels all actions."""
    ticks = re.findall(r'^WINDOW_REMAINING: (\d+)$', log, re.MULTILINE)
    remaining = int(ticks[-1]) if ticks else None
    if 'STOP_NOT_VERIFIED:' in log:
        return 'STOP_NOT_VERIFIED', '停车未确认：立即急停、关闭驱动电源。', remaining
    if 'APPLICATION_EXIT:' in log or 'APPLICATION_STOPPING:' in log or remaining == 0:
        return 'STOP', '应用已停止或窗口已结束，不再执行阶段动作；摇杆回中，等待清理后关闭驱动电源。', remaining
    phases = [(log.rfind(name + ':'), name, message) for name, message in PROMPTS]
    offset, name, message = max(phases)
    return (name, message, remaining) if offset >= 0 else ('WAIT', '正在准备采集和应用；保持中立，暂不操作 CH6、摇杆或发射机。', remaining)


def main():
    """Obtain fresh physical readiness locally before starting the bounded one-shot runner."""
    assert sys.stdin.isatty() and sys.stdout.isatty(), 'Visible interactive terminal required'
    assert not (BASE / 'recovery-jcan-once').exists(), 'This one-shot is already consumed'
    print('F6 A1：右轮运动 SIGTERM；右轮正向≤5rpm、不足3秒，2.95秒自动截止；左轮静止；静止反馈容差±2rpm。')
    print('执行器自动发一次信号；现场不做关遥控器/拔线/X1刺激。总输入就绪窗口60秒。')
    print('停车负反馈超±2rpm保留待复核；±7.5rpm硬界、对侧±2rpm与1秒内稳定停车仍强制检查。')
    for name, message in PROMPTS:
        print(f'  {name}: {message}')
    print('请确认：SBUS信号已接回、接线未变，发射机和接收机开启并全程供电；双轮架空卸载且无人触碰；')
    print('X1已复位，摇杆中立、CH6松开，驱动已上电，现场可立即急停/断电且能看清本窗口。')
    print('全部属实后输入 START；否则取消。出现异响或异常转动立即急停/断电，不等待软件。')
    answer = input('F6现场就绪 > ').strip()
    if answer != 'START':
        print('已取消，未开始试验。')
        return
    confirmation = dict(statement='F6右轮运动SIGTERM已上电就绪', raw_user_statement=answer,
                        confirmation_source='Local terminal START after F6 A1 checklist',
                        wall_time=time.time(), drive_power_on=True, x1_released=True,
                        no_wheel_contact=True, sticks_neutral=True, button_released=True,
                        wheels_raised=True, emergency_stop_available=True, unchanged_wiring=True,
                        operator_display_visible=True, receiver_stays_powered=True, transmitter_stays_on=True,
                        signal_reconnected=True, automatic_sigterm_stimulus=True, motion_window_ms=3000)
    with (BASE / 'operator-confirmation.json').open('x') as out:
        json.dump(confirmation, out, ensure_ascii=False, indent=2)
        out.write('\n')
    result = subprocess.run([sys.executable, '-B', '-u', str(BASE / 'run-recovery.py')], check=False)
    print(f'\n采集执行器已返回 {result.returncode}（SIGTERM停车结果待独立审查）；保持摇杆中立，请关闭驱动电源。')
    print('若右轮方向正常且已停、左轮始终未转、无异响、驱动已断电、发射机和接收机全程开启，未按X1或拔线，输入 OFF；否则描述实际情况。')
    answer = input('现场收尾 > ').strip()
    record = dict(raw_user_statement=answer, right_direction_and_stop_normal=True if answer == 'OFF' else None,
                  left_always_stationary=True if answer == 'OFF' else None,
                  drive_power_off=True if answer == 'OFF' else None,
                  no_abnormal_sound=True if answer == 'OFF' else None, transmitter_stayed_on=True if answer == 'OFF' else None, no_external_fault_stimulus=True if answer == 'OFF' else None, receiver_stayed_powered=True if answer == 'OFF' else None,
                  confirmation_source='Local terminal post-trial question', runner_exit=result.returncode)
    (BASE / 'operator-post-trial.json').write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n')
    action = input('可选：记录现场异常或实际操作差异 > ').strip()
    record['actual_operation'] = action
    (BASE / 'operator-post-trial.json').write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n')
    print('已保存现场反馈；应用通过与否还需独立证据审查。可关闭本窗口。')


if __name__ == '__main__':
    main()
