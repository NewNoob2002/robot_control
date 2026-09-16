"""Prepared 45-second receive-only Source HIL; requires NEW authorization and readiness."""
import hashlib
import json
from pathlib import Path
import signal
import subprocess
import time
from datetime import datetime, timezone

ROOT = Path('/home/cat/.cache/robot-control/staging/p93-source-e7b4cded-attempt2')
BINARY = ROOT / 'robot-control-sbus-observer'
OUT = ROOT / 'source-hil-45s-20260916-attempt2'
EXPECTED = 'e7b4cdedb9511f4b92c93fb5d6dc9b7a853afbdd851a4ab2adb1de0ea4779999'
PHASES = [(0, '开始：继续按住CH6', '两个摇杆中立，CH7中档。'), (3, '松开CH6', '两个摇杆保持中立。'), (6, 'CH6按下再松开一次', '保持中立，检查新的启用边沿。'), (9, '油门前推到中等位置并保持', '转向中立，CH6松开。'), (12, '油门回中', '两个摇杆保持中立。'), (15, 'CH6按下再松开一次', '保持中立，检查正常禁用。'), (18, '油门前推，再按下并松开CH6', '油门保持非中立；检查不能重新启用。'), (21, '油门回中，不按CH6', '观察回中后不会自动启用。'), (24, 'CH6按下再松开一次', '两个摇杆中立，重新启用输入。'), (27, '关闭发射机', '只关手柄中心电源，不动接收机或适配器。'), (33, '打开发射机', '两个摇杆中立，CH6松开，CH7中档；不要按CH6。'), (39, 'CH6按下再松开一次', '两个摇杆中立；检查恢复后新的启用边沿。'), (42, '油门前推到中等位置并保持', '保持至结束提示；程序结束后再回中。')]


def event(kind, title, body='', **fields):
    """Persist and flush a timestamped prompt to the local notification relay."""
    record = dict(kind=kind, title=title, body=body,
                  utc=datetime.now(timezone.utc).isoformat(), mono_ns=time.monotonic_ns(), **fields)
    text = json.dumps(record, ensure_ascii=False)
    with (OUT / 'timeline.jsonl').open('a') as log:
        log.write(text + '\n')
    print(text, flush=True)


def main():
    """Recheck identity, capture once, stop at 45 seconds, and retain all evidence."""
    OUT.mkdir(exist_ok=False)
    assert hashlib.sha256(BINARY.read_bytes()).hexdigest() == EXPECTED
    identity = subprocess.check_output(['udevadm', 'info', '--query=property', '--name=/dev/ttyACM0'], text=True)
    (OUT / 'adapter-before.txt').write_text(identity)
    assert 'ID_SERIAL_SHORT=586D017868\n' in identity
    holders = subprocess.run(['lsof', '-nP', '/dev/ttyACM0'], capture_output=True, text=True)
    (OUT / 'holders-before.txt').write_text(holders.stdout + holders.stderr)
    assert holders.returncode == 1 and not holders.stdout and not holders.stderr
    for remaining in (20, 10, 5, 3, 2, 1):
        event('countdown', f'{remaining} 秒后开始 SBUS 采集', '保持按住CH6，摇杆中立；收到松开提示再松开。')
        time.sleep({20:10, 10:5, 5:2, 3:1, 2:1, 1:1}[remaining])
    process = None
    try:
        with (OUT / 'capture.log').open('wb') as output, (OUT / 'capture.stderr').open('wb') as errors:
            started = time.monotonic_ns()
            process = subprocess.Popen([str(BINARY), '--device', '/dev/ttyACM0', '--duration-ms', '60000', '--steering-axis', '200,1000,1800,0', '--throttle-axis', '200,993,1800,0'],
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
            delay = (started + 45_000_000_000 - time.monotonic_ns()) / 1e9
            try:
                process.wait(timeout=max(0, delay))
                raise RuntimeError(f'observer exited early: {process.returncode}')
            except subprocess.TimeoutExpired:
                pass
            sent = time.monotonic_ns()
            process.send_signal(signal.SIGTERM)
            event('sigterm', '45秒结束：正在验证 SIGTERM', '程序正在退出；现在可以将摇杆回中。', signal_sent_ns=sent)
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
