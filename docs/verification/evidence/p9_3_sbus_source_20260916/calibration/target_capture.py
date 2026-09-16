"""Prepared 30-second calibration capture; requires NEW operator authorization and readiness."""
import hashlib
import json
from pathlib import Path
import signal
import subprocess
import time
from datetime import datetime, timezone

ROOT = Path('/home/cat/.cache/robot-control/staging/p93-calibration-bd76d3fd')
BINARY = ROOT / 'robot-control-sbus-observer'
OUT = ROOT / 'calibration-30s-20260916'
EXPECTED = 'bd76d3fd3d77d2ade41c4ba5e28a884fca5ab24a8b53947fd44db5e11cc03fe8'
PHASES = [(0, '开始：两个摇杆保持中立', '全程 CH6 松开、CH7 中档；不要操作微调按钮。'), (3, '油门向前推到底并保持', '转向保持中立，等待下一条提示。'), (7, '油门回中并保持', '两个摇杆中立。'), (9, '油门向后拉到底并保持', '转向保持中立，等待下一条提示。'), (13, '油门回中并保持', '两个摇杆中立。'), (15, '转向向右推到底并保持', '油门保持中立，等待下一条提示。'), (19, '转向回中并保持', '两个摇杆中立。'), (21, '转向向左推到底并保持', '油门保持中立，等待下一条提示。'), (25, '两个摇杆回中并保持至结束', 'CH6 松开、CH7 中档，不要关闭发射机。')]


def event(kind, title, body='', **fields):
    """Persist and flush a timestamped prompt to the local notification relay."""
    record = dict(kind=kind, title=title, body=body,
                  utc=datetime.now(timezone.utc).isoformat(), mono_ns=time.monotonic_ns(), **fields)
    text = json.dumps(record, ensure_ascii=False)
    with (OUT / 'timeline.jsonl').open('a') as log:
        log.write(text + '\n')
    print(text, flush=True)


def main():
    """Recheck identity, capture once, stop at 30 seconds, and retain all evidence."""
    OUT.mkdir(exist_ok=False)
    assert hashlib.sha256(BINARY.read_bytes()).hexdigest() == EXPECTED
    identity = subprocess.check_output(['udevadm', 'info', '--query=property', '--name=/dev/ttyACM0'], text=True)
    (OUT / 'adapter-before.txt').write_text(identity)
    assert 'ID_SERIAL_SHORT=586D017868\n' in identity
    holders = subprocess.run(['lsof', '-nP', '/dev/ttyACM0'], capture_output=True, text=True)
    (OUT / 'holders-before.txt').write_text(holders.stdout + holders.stderr)
    assert holders.returncode == 1 and not holders.stdout and not holders.stderr
    for remaining in (10, 5, 3, 2, 1):
        event('countdown', f'{remaining} 秒后开始 SBUS 采集', '准备按桌面通知依次操作。')
        time.sleep({10:5, 5:2, 3:1, 2:1, 1:1}[remaining])
    process = None
    try:
        with (OUT / 'capture.log').open('wb') as output, (OUT / 'capture.stderr').open('wb') as errors:
            started = time.monotonic_ns()
            process = subprocess.Popen([str(BINARY), '--device', '/dev/ttyACM0', '--duration-ms', '60000'],
                                       stdout=output, stderr=errors)
            (OUT / 'observer.pid').write_text(str(process.pid) + '\n')
            for seconds, title, body in PHASES:
                delay = (started + seconds * 1_000_000_000 - time.monotonic_ns()) / 1e9
                if delay > 0:
                    try:
                        process.wait(timeout=delay)
                        raise RuntimeError(f'observer exited early: {process.returncode}')
                    except subprocess.TimeoutExpired:
                        pass
                if process.poll() is not None:
                    raise RuntimeError(f'observer exited early: {process.returncode}')
                event('phase', title, body, planned_second=seconds, start_mono_ns=started)
            delay = (started + 30_000_000_000 - time.monotonic_ns()) / 1e9
            try:
                process.wait(timeout=max(0, delay))
                raise RuntimeError(f'observer exited early: {process.returncode}')
            except subprocess.TimeoutExpired:
                pass
            sent = time.monotonic_ns()
            process.send_signal(signal.SIGTERM)
            event('sigterm', '30秒结束：正在验证 SIGTERM', '保持发射机开启、摇杆中立。', signal_sent_ns=sent)
            code = process.wait(timeout=1)
            ended = time.monotonic_ns()
            result = dict(exit_code=code, run_to_signal_ms=(sent-started)/1e6,
                          signal_to_reaped_ms=(ended-sent)/1e6, observer_pid=process.pid,
                          start_mono_ns=started, signal_sent_ns=sent, reaped_ns=ended)
            (OUT / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
            assert code == 143, result
            event('complete', '采集完成：可以停止操作', '发射机保持开启；观察工具已退出，正在分析数据。', **result)
    except BaseException:
        event('failure', '采集停止：请将发射机打开、摇杆回中', '本次失败会保留记录，不自动重试。')
        raise
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=2)
        holders = subprocess.run(['lsof', '-nP', '/dev/ttyACM0'], capture_output=True, text=True)
        (OUT / 'holders-after.txt').write_text(holders.stdout + holders.stderr)


if __name__ == '__main__':
    main()
