"""Direct, local operator instructions for one zero-only X1 trial."""
from pathlib import Path
import json
import re
import subprocess
import sys
import time

BASE = Path(__file__).resolve().parent
PROMPTS = (
    ('CONTROL_READY', '摇杆保持中立，CH6 按一次后松开；等待下一提示。'),
    ('FAULT_READY', '现在按锁 X1 急停；保持锁定，等待解除提示。'),
    ('RELEASE_FAULT', '解除 X1；油门向前推足并保持，再按 CH6 一次后松开；等待回中提示。'),
    ('NEUTRAL_READY', '现在摇杆回中，CH6 保持松开；等待重新授权提示。'),
    ('REARM_READY', '保持摇杆中立，CH6 再按一次后松开。'),
    ('ZERO_REENABLED', '已重新零目标使能；保持中立，等待清理。'),
    ('RECOVERY_COMPLETE', '恢复观察完成；保持中立，等待应用退出和配置恢复。'),
)


def current_prompt(log):
    """Return only the latest live phase; an exit or expired window cancels all actions."""
    ticks = re.findall(r'^WINDOW_REMAINING: (\d+)$', log, re.MULTILINE)
    remaining = int(ticks[-1]) if ticks else None
    if 'APPLICATION_EXIT:' in log or 'APPLICATION_STOPPING:' in log or remaining == 0:
        return 'STOP', '试验窗口已结束，不再执行阶段动作；摇杆回中，等待清理后关闭驱动电源。', remaining
    phases = [(log.rfind(name + ':'), name, message) for name, message in PROMPTS]
    offset, name, message = max(phases)
    return (name, message, remaining) if offset >= 0 else ('WAIT', '正在准备采集和应用；保持中立，暂不操作 CH6 或 X1。', remaining)


def main():
    """Obtain fresh physical readiness locally before starting the bounded one-shot runner."""
    assert sys.stdin.isatty() and sys.stdout.isatty(), 'Visible interactive terminal required'
    assert not (BASE / 'recovery-jcan-once').exists(), 'This one-shot is already consumed'
    print('F1 A3：零目标 X1 恢复验证 — 全程双轮应静止；目标严格为零，速度反馈容差 ±1 rpm；总窗口 60 秒。\n')
    print('请先阅读步骤；运行后直接按本窗口的新提示操作，不等待聊天转达：')
    for name, message in PROMPTS:
        print(f'  {name}: {message}')
    print('\n出现意外转动/异响，立即现场急停、断电；不要等待软件。')
    print('\n启动前请确认：接线未变，双轮架空卸载且无人触碰，现场可立即急停/断电；')
    print('摇杆中立、CH6 松开、X1 已解除复位，驱动已上电就绪，能看清本窗口。')
    print('准备好且上述全部属实后输入 START 回车；其他输入取消，不开始采集或控制。')
    answer = input('就绪确认 > ').strip()
    if answer != 'START':
        print('已取消；未开始试验。')
        return
    confirmation = dict(statement='F1零目标X1恢复已上电就绪', raw_user_statement=answer,
                        confirmation_source='Local interactive terminal after displayed A3 checklist',
                        wall_time=time.time(), drive_power_on=True, x1_released=True,
                        no_wheel_contact=True, sticks_neutral=True, button_released=True,
                        wheels_raised=True, emergency_stop_available=True, unchanged_wiring=True,
                        operator_display_visible=True)
    with (BASE / 'operator-confirmation.json').open('x') as out:
        json.dump(confirmation, out, ensure_ascii=False, indent=2)
        out.write('\n')
    result = subprocess.run([sys.executable, '-B', '-u', str(BASE / 'run-recovery.py')], check=False)
    print(f'\n执行器已返回 {result.returncode}；保持摇杆中立，请关闭驱动电源。')
    print('若双轮始终未转、无异常声音且电源现已关闭，输入 OFF；否则描述实际情况。')
    answer = input('现场收尾 > ').strip()
    record = dict(raw_user_statement=answer, wheels_stationary=True if answer == 'OFF' else None,
                  abnormal_sound=False if answer == 'OFF' else None,
                  drive_power_off=True if answer == 'OFF' else None,
                  confirmation_source='Local terminal post-trial question', runner_exit=result.returncode)
    (BASE / 'operator-post-trial.json').write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n')
    print('已保存现场反馈；应用通过与否还需独立证据审查。可关闭本窗口。')


if __name__ == '__main__':
    main()
