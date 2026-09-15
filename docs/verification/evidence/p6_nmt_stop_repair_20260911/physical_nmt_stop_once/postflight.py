"""Read target identity, residual processes and CAN counters after the consumed trial."""
import json
import shlex
import subprocess
from pathlib import Path
out = Path(__file__).resolve().parent
code = """
import json,subprocess
from pathlib import Path
assert Path('/etc/machine-id').read_text().strip() == '6923ab3301fb4a8d816759b04ec6bf0a'
remaining=[]
for p in Path('/proc').iterdir():
    if not p.name.isdigit(): continue
    try:
        name=(p/'exe').resolve(strict=True).name
        if name in ('robot-control-zlac-qualification','robot-control-canopen-commission','candump','cansend','cangen'):
            remaining.append({'pid':p.name,'executable':name})
    except (FileNotFoundError,PermissionError,ProcessLookupError): pass
can=json.loads(subprocess.check_output(['ip','-j','-details','-statistics','link','show','can0'],text=True))
print(json.dumps({'residual_processes':remaining,'can':can}))
"""
result=subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=5','robot-dev',shlex.join(['python3','-c',code])],capture_output=True,text=True,timeout=15,check=True)
assert not result.stderr
(out/'target_process_postflight.json').write_text(result.stdout)
data=json.loads(result.stdout)
assert not data['residual_processes']
assert data['can'][0]['stats64']==json.loads((out/'target/can_postflight.json').read_text())[0]['stats64']
print('Postflight PASS: no residual process or additional CAN packets')
