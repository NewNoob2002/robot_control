"""Direct, local operator instructions for one right-wheel moving transmitter-loss trial."""
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
    ('MOTION_READY', '只向前推油门，转向保持中立；右轮开始转动后保持油门，等待 POWER_OFF_READY 关机提示。运动硬上限8秒，失联会提前停车；不要按 X1。'),
    ('POWER_OFF_READY', '已观察到右轮正向反馈约1秒：现在长按手持遥控器电源键直到关机，接收机保持供电。关机前保持油门，确认关机后摇杆回中；不要等8秒截止，不再 CH6。异常时立即 X1/断电。'),
    ('STOP_VERIFIED', '已观察到停车；摇杆回中，发射机保持关闭、接收机保持供电，等待清理后关闭驱动电源。'),
    ('STOP_NOT_VERIFIED', '停车未确认，立即现场急停、关闭驱动电源。'),
)


def current_prompt(log):
    """Return only the latest live phase; an exit or expired window cancels all actions."""
    ticks = re.findall(r'^WINDOW_REMAINING: (\d+)$', log, re.MULTILINE)
    remaining = int(ticks[-1]) if ticks else None
    if 'APPLICATION_EXIT:' in log or 'APPLICATION_STOPPING:' in log or remaining == 0:
        return 'STOP', '应用已停止或窗口已结束，不再执行阶段动作；摇杆回中，等待清理后关闭驱动电源。', remaining
    phases = [(log.rfind(name + ':'), name, message) for name, message in PROMPTS]
    offset, name, message = max(phases)
    return (name, message, remaining) if offset >= 0 else ('WAIT', '正在准备采集和应用；保持中立，暂不操作 CH6、摇杆或发射机。', remaining)


def main():
    """Obtain fresh physical readiness locally before starting the bounded one-shot runner."""
    assert sys.stdin.isatty() and sys.stdout.isatty(), 'Visible interactive terminal required'
    assert not (BASE / 'recovery-jcan-once').exists(), 'This one-shot is already consumed'
    print('本次关闭的是手持遥控器（发射机）电源；不要按 X1。X1 仅用于意外时紧急停车。')
    print('F4 A6：右轮运动 SBUS 发射机失联停车 — 右轮正向 ≤5rpm / 不足8秒（7.95秒自动截止），左轮始终静止；反馈静止容差±2rpm；总就绪窗口60秒。\n')
    print('停车后选中轮瞬时负反馈超±2rpm将记录待复核；运动保护、对侧静止、±7.5rpm硬界和1秒停车确认保持有效。')
    print('请先阅读步骤；运行后直接按本窗口的新提示操作，不等待聊天转达：')
    for name, message in PROMPTS:
        print(f'  {name}: {message}')
    print('\n出现意外转动/异响，立即现场急停、断电；不要等待软件。')
    print('\n启动前请确认：接线未变，双轮架空卸载且无人触碰，现场可立即急停/断电；')
    print('摇杆中立、CH6 松开、X1 已解除复位，接收机已开启并全程供电；遥控器可以尚未开机，启动后按 INPUT_WAIT 提示开启并等待健康连接，能够在 POWER_OFF_READY 提示后长按电源键完成关机，驱动已上电就绪，能看清本窗口。')
    print('准备好且上述全部属实后输入 START 回车；随后还须确认本次操作是关闭遥控器，其他输入取消。')
    answer = input('就绪确认 > ').strip()
    if answer != 'START' or input('请确认X1已旋转复位，输入 X1已复位，关闭遥控器 > ').strip() != 'X1已复位，关闭遥控器':
        print('已取消；未开始试验。')
        return
    confirmation = dict(statement='F4右轮运动SBUS失联已上电就绪', raw_user_statement=answer,
                        confirmation_source='Local terminal START plus typed X1已复位，关闭遥控器 after F4 A6 checklist',
                        wall_time=time.time(), drive_power_on=True, x1_released=True,
                        no_wheel_contact=True, sticks_neutral=True, button_released=True,
                        wheels_raised=True, emergency_stop_available=True, unchanged_wiring=True,
                        operator_display_visible=True, receiver_stays_powered=True, transmitter_only_stimulus=True,
                        motion_window_ms=8000, power_off_prompt_after_positive_feedback_ms=1000)
    with (BASE / 'operator-confirmation.json').open('x') as out:
        json.dump(confirmation, out, ensure_ascii=False, indent=2)
        out.write('\n')
    result = subprocess.run([sys.executable, '-B', '-u', str(BASE / 'run-recovery.py')], check=False)
    print(f'\n采集执行器已返回 {result.returncode}（失联停车结果待独立审查）；保持摇杆中立，请关闭驱动电源。')
    print('若右轮方向正常且已停、左轮始终未转、无异响、驱动已断电、发射机仍关闭且接收机全程供电，输入 OFF；否则描述实际情况。')
    answer = input('现场收尾 > ').strip()
    record = dict(raw_user_statement=answer, right_direction_and_stop_normal=True if answer == 'OFF' else None,
                  left_always_stationary=True if answer == 'OFF' else None,
                  drive_power_off=True if answer == 'OFF' else None,
                  no_abnormal_sound=True if answer == 'OFF' else None, transmitter_off=True if answer == 'OFF' else None, receiver_stayed_powered=True if answer == 'OFF' else None,
                  confirmation_source='Local terminal post-trial question', runner_exit=result.returncode)
    (BASE / 'operator-post-trial.json').write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n')
    action = input('请记录实际操作：启动时遥控器是否未开机、等待提示是否正确；运动中是否按 POWER_OFF_READY 长按关机、关机前是否回中、接收机是否全程供电、是否使用 X1 > ').strip()
    record['actual_operation'] = action
    (BASE / 'operator-post-trial.json').write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n')
    print('已保存现场反馈；应用通过与否还需独立证据审查。可关闭本窗口。')


if __name__ == '__main__':
    main()
