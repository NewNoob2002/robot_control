"""Bounded silent capture for the P10.3 zero-motion diagnostics prerequisite."""
import json
import os
from pathlib import Path
import selectors
import subprocess
import time

BASE = Path(__file__).resolve().parent
OUT = BASE / 'diagnostics-jcan-once'
JCAN = '/home/gtc/Desktop/workspace/JCAN/target/release/jcan'
SERIAL = '207F346D5650'


def main():
    """Receive independently for at most 35 seconds; send no CAN and never retry the trial."""
    OUT.mkdir(exist_ok=False)
    process = subprocess.Popen(['pkexec', JCAN, '--json', '--serial', SERIAL,
                                'session', '--mode', 'silent', '--receive'],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ, 'stdout')
    selector.register(process.stderr, selectors.EVENT_READ, 'stderr')
    buffers = {'stdout': b'', 'stderr': b''}
    connected = receiving = False
    deadline = time.monotonic() + 60  # Authentication only; the actual receive window is 35s.
    result = {'passed': False, 'frames': 0, 'serial': SERIAL, 'mode': 'silent'}
    try:
        with (OUT / 'session.jsonl').open('wb') as log, (OUT / 'stderr.log').open('wb') as err:
            while not (OUT / 'stop').exists():
                assert time.monotonic() < deadline, 'bounded capture window expired'
                assert process.poll() is None, 'JCAN exited before stop request'
                for key, _ in selector.select(timeout=0.05):
                    data = os.read(key.fileobj.fileno(), 65536)
                    assert data, 'JCAN stream closed before stop request'
                    label = key.data
                    (log if label == 'stdout' else err).write(data)
                    (log if label == 'stdout' else err).flush()
                    assert label == 'stdout', data.decode(errors='replace')
                    buffers[label] += data
                    assert len(buffers[label]) < 1000000, 'unterminated JSON stream'
                    while b'\n' in buffers[label]:
                        line, buffers[label] = buffers[label].split(b'\n', 1)
                        record = json.loads(line)
                        assert record.get('ok') is not False and not record.get('error') and not record.get('warnings'), record
                        event = record.get('event')
                        if event == 'connected':
                            assert not connected, 'reconnection is not a new capture permit'
                            assert record['serial'] == SERIAL and record['mode'] == 'silent', record
                            connected = True
                        elif event == 'session_started':
                            assert record['receive_enabled'] is True
                            receiving = True
                        elif event == 'frame':
                            result['frames'] += 1
                            assert result['frames'] <= 10000, 'capture frame budget exceeded'
                        else:
                            raise AssertionError(record)
                        if connected and receiving and not (OUT / 'ready.json').exists():
                            deadline = time.monotonic() + 35
                            (OUT / 'ready.json').write_text(json.dumps({'wall_time': time.time(), 'serial': SERIAL}))
                            print('JCAN_READY silent receive; maximum35s', flush=True)
            result['passed'] = True
    except BaseException as exc:
        result['error'] = str(exc)
        raise
    finally:
        if process.poll() is None:
            process.stdin.write((json.dumps({'id': 99, 'op': 'shutdown'}) + chr(10)).encode())
            process.stdin.flush()
            process.stdin.close()
            # Retain the shutdown response and any buffered final frames.
            process.stdin = None
            tail, errors = process.communicate(timeout=5)
            with (OUT / 'session.jsonl').open('ab') as log:
                log.write(tail)
            with (OUT / 'stderr.log').open('ab') as log:
                log.write(errors)
            assert not errors, errors.decode(errors='replace')
        result['exit_code'] = process.returncode
        result['passed'] = result['passed'] and process.returncode == 0
        (OUT / 'result.json').write_text(json.dumps(result, indent=2) + chr(10))
        selector.close()
        print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
