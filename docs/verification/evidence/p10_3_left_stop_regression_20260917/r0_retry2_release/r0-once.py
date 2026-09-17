"""One bounded, explicitly authorized UART-only R0 capture; never opens/configures CAN."""
from pathlib import Path
import hashlib
import json
import signal
import subprocess
import time

BASE=Path(__file__).resolve().parent
ARGS=['--interface','none','--device','/dev/serial/by-id/usb-1a86_USB_Single_Serial_586D017868-if00','--duration-ms','60000','--observe-input']


def main():
    """Verify identity/hash/exclusive input ownership, consume once and capture for60s."""
    auth=json.loads((BASE/'authorization.json').read_text())
    assert auth['authorized'] and auth['attempts']==1 and auth['arguments']==ARGS
    assert Path('/etc/machine-id').read_text().strip()=='6923ab3301fb4a8d816759b04ec6bf0a'
    binary=BASE/'robot-control-hil'
    assert hashlib.sha256(binary.read_bytes()).hexdigest()==auth['artifact_sha256']
    properties=subprocess.check_output(['udevadm','info','-q','property','-n',ARGS[3]],text=True,timeout=5)
    assert 'ID_SERIAL_SHORT=586D017868' in properties
    for entry in Path('/proc').iterdir():
        if entry.name.isdigit():
            try:
                name=(entry/'exe').resolve(strict=True).name
                assert name not in ('robot-control-hil','robot-control-sbus-observer','robot-control-zlac-qualification','robot-control-canopen-commission','cansend','cangen'),name
            except (FileNotFoundError,PermissionError,ProcessLookupError):pass
    out=BASE/'capture-once'
    out.mkdir(exist_ok=False)
    result={'passed':False,'scope':'UART-only R0; no drive lifecycle','arguments':ARGS,'artifact_sha256':auth['artifact_sha256']}
    application=None
    try:
        with (out/'application.log').open('wb') as log:
            started=time.monotonic()
            application=subprocess.Popen([str(binary),*ARGS],stdout=log,stderr=subprocess.STDOUT)
            (out/'application.pid').write_text(str(application.pid)+'\n')
            while application.poll() is None:
                assert time.monotonic()-started<75,'receive-only capture exceeded75s'
                if not result.get('ready') and 'event=input_observation_ready' in (out/'application.log').read_text():
                    result['ready']=True
                    print('R0_READY: drive OFF; CH6 released; Push forward/right together, hold2s, release naturally, then remain neutral;60s',flush=True)
                time.sleep(0.02)
            print('R0_DONE: capture ended; remain neutral, drive OFF',flush=True)
            result['application_exit']=application.returncode
            result['elapsed_ms']=(time.monotonic()-started)*1000
            assert application.returncode==0,'R0 application failed; inspect evidence, no automatic retry'
            result['passed']=True
    except BaseException as error:
        result['error']=str(error)
        raise
    finally:
        if application is not None and application.poll() is None:
            application.send_signal(signal.SIGTERM)
            try:application.wait(timeout=5)
            except subprocess.TimeoutExpired:
                application.kill();application.wait(timeout=2)
                result['forced_kill']=True
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result),flush=True)


if __name__=='__main__':main()
