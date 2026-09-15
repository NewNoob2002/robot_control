"""Exercise the manual cable runner's capture and completion guards without hardware."""
import ast
import json
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
root=Path(__file__).parent
tree=ast.parse((root/'remote_trial.py').read_text())
guards=[node for node in ast.walk(tree) if isinstance(node,ast.Assert)]
capture=next(node for node in guards if isinstance(node.msg,ast.Constant) and node.msg.value=='unexpected capture warning')
completion=next(node for node in guards if isinstance(node.msg,ast.Constant) and node.msg.value=='expected external loss/recovery absent')
with TemporaryDirectory() as temporary:
    out=Path(temporary)
    code=compile(ast.Expression(capture.test),'actual_capture_guard','eval')
    for warning in ('','can0: interface down'+chr(10),'capture overflow'+chr(10)):
        (out/'capture.stderr').write_text(warning)
        assert bool(eval(code,{'OUT':out}))==(warning=='')
code=compile(ast.Expression(completion.test),'actual_completion_guard','eval')
for rc,count,accepted in ((0,1,True),(1,1,False),(0,0,False),(0,2,False)):
    assert bool(eval(code,{'child':SimpleNamespace(returncode=rc),'expected':['complete']*count}))==accepted
for name in ('local_trial.py','remote_trial.py'):
    ast.parse((root/name).read_text())
authorization=json.loads((root/'authorization.json').read_text())
assert authorization['stimulus']=='cable_loss' and authorization['attempts']==1
print('PASS: unexpected capture diagnostics and missing/failed/duplicate completion rejected; no hardware access')
