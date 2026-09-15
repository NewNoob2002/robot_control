"""Exercise the actual terminal arm guard at the extended-window boundaries without hardware."""
import ast
from pathlib import Path
from types import SimpleNamespace
tree=ast.parse((Path(__file__).parent/'interface_toggle.py').read_text())
guard=next(node for node in tree.body if isinstance(node,ast.Assert) and 'moving_tpdo_observed' in ast.unparse(node.test))
code=compile(ast.Expression(guard.test),'actual_arm_guard','eval')
for age,expected in ((0,True),(5.9,True),(6,False),(8,False),(-1,False)):
    scope={'data':{'elf_sha256':'hash','moving_tpdo_observed':True,'wall_time':100},'EXPECTED':'hash','time':SimpleNamespace(time=lambda:100+age)}
    assert bool(eval(code,scope))==expected
    scope['data']['moving_tpdo_observed']=False
    assert not eval(code,scope)
print('PASS: fresh late trigger accepted; expired/future/unconfirmed-motion arm rejected; no hardware access')
