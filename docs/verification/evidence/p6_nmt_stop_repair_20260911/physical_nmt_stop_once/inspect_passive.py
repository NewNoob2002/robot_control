"""Inspect the remaining passive candump without stopping unrelated processes."""
import json,shlex,subprocess
from pathlib import Path
out=Path(__file__).resolve().parent
code="""
import json,os
from pathlib import Path
p=Path('/proc/35900')
assert (p/'exe').resolve().name=='candump'
boot=next(int(line.split()[1]) for line in Path('/proc/stat').read_text().splitlines() if line.startswith('btime '))
fields=(p/'stat').read_text().split(') ',1)[1].split()
start=boot+int(fields[19])/os.sysconf('SC_CLK_TCK')
print(json.dumps({'pid':35900,'start_epoch_seconds':start,'cmdline':(p/'cmdline').read_bytes().replace(bytes([0]),b' ').decode(),'stdout':os.readlink(p/'fd/1')}))
"""
r=subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=5','robot-dev',shlex.join(['python3','-c',code])],capture_output=True,text=True,timeout=15,check=True)
assert not r.stderr
(out/'existing_passive_process.json').write_text(r.stdout)
data=json.loads(r.stdout)
trial=float((out/'target/attempt_started.txt').read_text())
assert data['start_epoch_seconds']<trial
print('Pre-existing passive candump predates this trial by',round(trial-data['start_epoch_seconds'],1),'seconds; left untouched')
post=json.loads((out/'target_process_postflight.json').read_text())
assert post['residual_processes']==[{'pid':'35900','executable':'candump'}]
assert post['can'][0]['stats64']==json.loads((out/'target/can_postflight.json').read_text())[0]['stats64']
(out/'process_cleanup_result.json').write_text(json.dumps({'passed':True,'owned_executor_and_capture_exited':True,'unrelated_passive_process_preserved':35900,'can_counters_unchanged':True,'initial_check_limitation':'The initial check rejected every candump, including this independently running pre-existing observer.'},indent=2))
print('Owned process cleanup PASS; CAN counters unchanged')
