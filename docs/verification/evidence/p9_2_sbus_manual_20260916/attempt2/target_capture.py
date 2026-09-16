"""One authorized 30-second receive-only run with timed human phases and SIGTERM."""
import hashlib
import json
from pathlib import Path
import signal
import subprocess
import time
from datetime import datetime, timezone

ROOT = Path('/home/cat/.cache/robot-control/staging/p92-byte-budget-bd76d3fd')
BINARY = ROOT / 'robot-control-sbus-observer'
OUT = ROOT / 'manual-30s-20260916'
EXPECTED = 'bd76d3fd3d77d2ade41c4ba5e28a884fca5ab24a8b53947fd44db5e11cc03fe8'
PHASES = [
    (0, '开始：摇杆回中', 'CH6松开，CH7中档。采集已开始。'),
    (3, '转向摇杆：左右拨动，再回中', '只操作用于转向的轴，持续到下一条提示。'),
    (8, '油门摇杆：上下拨动，再回中', '只操作用于油门的轴，持续到下一条提示。'),
    (13, 'CH6：按下、松开两次', '两个摇杆保持中立。'),
    (16, '只操作标记CH7的开关', '低→中→高→中；CH6及微调按钮保持不动。'),
    (20, '现在关闭发射机', '接收机和适配器继续供电；等待下一条开机提示。'),
    (24, '现在打开发射机', '摇杆中立，CH6松开，CH7中档；等待恢复。'),
]


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
