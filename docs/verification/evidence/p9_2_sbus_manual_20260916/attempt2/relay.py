"""Display target-timed phase notifications without relying on chat timing."""
import json
from pathlib import Path
import subprocess
import time

out = Path('out/p92-manual-30s-retry-20260916')
command = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5', 'robot-dev',
           'timeout --signal=TERM --kill-after=3s 50s python3 /home/cat/.cache/robot-control/staging/p92-byte-budget-bd76d3fd/target_capture_30s.py']
with (out / 'runner.stderr').open('w') as errors, (out / 'relay.jsonl').open('w') as transcript:
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=errors, text=True, bufsize=1)
    notification = None
    for line in process.stdout:
        event = json.loads(line)
        event['local_received_ns'] = time.monotonic_ns()
        args = ['notify-send', '--print-id', '--app-name=SBUS 验证', '--urgency=critical', '--expire-time=6000']
        if notification is not None:
            args += ['--replace-id', notification]
        result = subprocess.run(args + [event['title'], event['body']], capture_output=True, text=True, timeout=2)
        event['notification_exit_code'] = result.returncode
        if result.returncode == 0:
            notification = result.stdout.strip()
        transcript.write(json.dumps(event, ensure_ascii=False) + '\n')
        transcript.flush()
        print(event['kind'] + ': ' + event['title'], flush=True)
    code = process.wait(timeout=5)
    (out / 'runner-exit-code.txt').write_text(str(code) + '\n')
    raise SystemExit(code)
