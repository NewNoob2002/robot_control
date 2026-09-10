#!/usr/bin/env python3
"""Check the manual-capture frame guard without opening hardware."""
import ast
from pathlib import Path
source=ast.parse((Path(__file__).parent/'local_trial.py').read_text())
guard=next(n for n in source.body if isinstance(n,ast.FunctionDef) and n.name=='validate_frame')
ns=dict(go=True,frames=0,downloads=0,uploads=0,nmts=0,nonzero=0,terminal=0)
exec(compile(ast.Module(body=[guard],type_ignores=[]),'guard','exec'),ns)

def check(payload,accepted=True,identifier=0x601,**flags):
    """Exercise one candidate while resetting cumulative counters between independent checks."""
    for key in ('frames','downloads','uploads','nmts','nonzero','terminal'): ns[key]=0
    try: ns['validate_frame']({'can_id':identifier,'data_hex':payload,**flags})
    except (AssertionError,RuntimeError): assert not accepted,payload
    else: assert accepted,payload

for payload in ['2300180181010080','2300180181010000','2F001A0000000000',
                '23001A0120036C60','23001A0220004160','23001A0120004160',
                '23001A0220036C60','2F001A0002000000','2F001802FF000000',
                '2B00180564000000','2B171000F4010000','2B17100000000000',
                '40001A0000000000','4000180100000000','406C600200000000']:
    check(payload)
for payload in ['23FF600205000000','23FF600200000000','2B4060000F000000',
                '2B40600006000000','2B002000E8030000','2B00180500000000',
                '2300180182010000','23011A0120036C60','2B10100173617665']:
    check(payload,False)
check('2300180181010000',False,fd=True)
check('2300180181010000',False,extended=True)
check('0101',identifier=0)
check('8001',identifier=0)
check('8101',False,identifier=0)
check('',identifier=0x281)
print('PASS: manual-only gate accepts reviewed mapping/readback, rejects all targets/controlwords/watchdog/persistent writes')
