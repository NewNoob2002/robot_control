#!/usr/bin/env python3
"""Exercise the restoration evidence oracle with actual virtual application traffic and tampering."""
from pathlib import Path
import json
import os
import runpy
import tempfile

BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[4]


def main():
    """Run one managed-vcan restoration and independently reject target/map mutations."""
    assert os.environ.get('ROBOT_CONTROL_TEST_VCAN_INTERFACE') == 'vcan0'
    peer = runpy.run_path(str(ROOT/'tests/integration/control_hil_zero_vcan.py'))
    original = peer['check_trace']
    oracle = runpy.run_path(str(BASE/'analyze-restore.py'))
    def inspect(wire, text, scenario):
        """Audit the real captured virtual stream without any physical device access."""
        original(wire, text, scenario)
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            (base/'recovery-target').mkdir()
            (base/'recovery-jcan-once').mkdir()
            (base/'recovery-target/application.log').write_text(text)
            (base/'recovery-target/result.json').write_text(json.dumps({'passed':True,'application_exit':0}))
            oracle['main'].__globals__['BASE'] = base
            for mutation in ('none','target','map'):
                rows = list(wire)
                if mutation != 'none':
                    idx = next(i for i,(_,ident,p) in enumerate(rows) if ident == 0x601 and p[0] != 0x40 and
                               int.from_bytes(p[1:3],'little') == (0x60ff if mutation == 'target' else 0x1600) and p[3] == (1 if mutation == 'target' else 2))
                    t,ident,p = rows[idx]
                    rows[idx] = (t,ident,p[:4]+(1).to_bytes(4,'little'))
                (base/'recovery-target/candump.log').write_text(''.join(f'({t:.6f}) can0 {i:03X} [{len(p)}] {p.hex(" ")}\n' for t,i,p in rows))
                (base/'recovery-jcan-once/session.jsonl').write_text(''.join(json.dumps(dict(event='frame',can_id=i,data_hex=p.hex(),brs=False,extended=False,fd=False,remote=False))+'\n' for _,i,p in rows))
                try:
                    oracle['main']()
                except AssertionError:
                    assert mutation != 'none'
                else:
                    assert mutation == 'none', 'Invalid mutated evidence accepted'
    peer['trial'].__globals__['check_trace'] = inspect
    peer['trial']('restore_baseline')
    print('restoration oracle: virtual replay and two mutation rejections PASS')


if __name__ == '__main__':
    main()
