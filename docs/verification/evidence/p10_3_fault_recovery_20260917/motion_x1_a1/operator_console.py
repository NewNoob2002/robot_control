"""Direct, local operator instructions for one right-wheel moving X1 trial."""
from pathlib import Path
import json
import re
import subprocess
import sys
import time

BASE = Path(__file__).resolve().parent
PROMPTS = (
    ('CONTROL_READY', '摇杆中立，CH6 按一次后松开，等待运动提示。'),
    ('MOTION_READY', '仅右轮：油门向前并配合左转向；右轮一开始转动就立即按锁 X1，随后摇杆回中。保持 X1 锁定直到退出并关闭驱动电源；不要再次 CH6。'),
    ('STOP_VERIFIED', '已观察到停车；摇杆回中，X1 保持锁定，等待清理后关闭驱动电源。'),
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
    return (name, message, remaining) if offset >= 0 else ('WAIT', '正在准备采集和应用；保持中立，暂不操作 CH6、摇杆或 X1。', remaining)


def main():
    """Obtain fresh physical readiness locally before starting the bounded one-shot runner."""
    assert sys.stdin.isatty() and sys.stdout.isatty(), 'Visible interactive terminal required'
    assert not (BASE / 'recovery-jcan-once').exists(), 'This one-shot is already consumed'
    print('F3 A1：右轮运动 X1 急停 — 右轮正向 ≤5rpm / 不足3秒，左轮始终静止；总就绪窗口60秒。\n')
    print('请先阅读步骤；运行后直接按本窗口的新提示操作，不等待聊天转达：')
    for name, message in PROMPTS:
        print(f'  {name}: {message}')
    print('\n出现意外转动/异响，立即现场急停、断电；不要等待软件。')
    print('\n启动前请确认：接线未变，双轮架空卸载且无人触碰，现场可立即急停/断电；')
    print('摇杆中立、CH6 松开、X1 已解除复位，遥控器和接收机均已开启且保持供电；能够在右轮开始转动后立即按锁 X1，驱动已上电就绪，能看清本窗口。')
    print('准备好且上述全部属实后输入 START 回车；其他输入取消，不开始采集或控制。')
    answer = input('就绪确认 > ').strip()
    if answer != 'START':
        print('已取消；未开始试验。')
        return
    confirmation = dict(statement='F3右轮运动X1已上电就绪', raw_user_statement=answer,
                        confirmation_source='Local interactive terminal after displayed F3 A1 checklist',
                        wall_time=time.time(), drive_power_on=True, x1_released=True,
                        no_wheel_contact=True, sticks_neutral=True, button_released=True,
                        wheels_raised=True, emergency_stop_available=True, unchanged_wiring=True,
                        operator_display_visible=True, receiver_stays_powered=True, motion_x1_stimulus=True)
    with (BASE / 'operator-confirmation.json').open('x') as out:
        json.dump(confirmation, out, ensure_ascii=False, indent=2)
        out.write('\n')
    result = subprocess.run([sys.executable, '-B', '-u', str(BASE / 'run-recovery.py')], check=False)
    print(f'\n执行器已返回 {result.returncode}；保持摇杆中立，请关闭驱动电源。')
    print('若右轮方向正常且急停后已停、左轮始终未转、无异响、驱动已断电且 X1 仍锁定，输入 OFF；否则描述实际情况。')
    answer = input('现场收尾 > ').strip()
    record = dict(raw_user_statement=answer, right_direction_and_stop_normal=True if answer == 'OFF' else None,
                  left_always_stationary=True if answer == 'OFF' else None,
                  drive_power_off=True if answer == 'OFF' else None,
                  no_abnormal_sound=True if answer == 'OFF' else None, x1_locked=True if answer == 'OFF' else None,
                  confirmation_source='Local terminal post-trial question', runner_exit=result.returncode)
    (BASE / 'operator-post-trial.json').write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n')
    action = input('请记录实际操作：是否在右轮转动期间按锁 X1，是否始终锁定到断电 > ').strip()
    record['actual_operation'] = action
    (BASE / 'operator-post-trial.json').write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n')
    print('已保存现场反馈；应用通过与否还需独立证据审查。可关闭本窗口。')


if __name__ == '__main__':
    main()
