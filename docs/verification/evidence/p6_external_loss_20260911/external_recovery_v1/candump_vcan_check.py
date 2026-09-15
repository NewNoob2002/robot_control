"""Check the exact target candump survives namespace-local interface down/up."""
import json,subprocess,time,signal
subprocess.run(['ip','link','add','vcan0','type','vcan'],check=True)
subprocess.run(['ip','link','set','vcan0','up'],check=True)
p=subprocess.Popen(['candump','-D','-ta','-e','vcan0,0:0,#FFFFFFFF'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
try:
 time.sleep(.1)
 subprocess.run(['cansend','vcan0','123#01'],check=True)
 time.sleep(.05)
 subprocess.run(['ip','link','set','vcan0','down'],check=True)
 time.sleep(.1)
 subprocess.run(['ip','link','set','vcan0','up'],check=True)
 time.sleep(.1)
 subprocess.run(['cansend','vcan0','123#02'],check=True)
 time.sleep(.1)
 assert p.poll() is None
finally:
 if p.poll() is None: p.send_signal(signal.SIGINT)
 out,err=p.communicate(timeout=2)
print(json.dumps({'rc':p.returncode,'stdout':out.decode(),'stderr':err.decode()}))
assert p.returncode == 0 and len(out.splitlines()) == 2
