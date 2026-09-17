from pathlib import Path
import hashlib,json
p=Path('/home/cat/.cache/robot-control/staging/p103-left-3c2d6a0f-20260917')
assert not (p/'physical-once').exists()
assert hashlib.sha256((p/'motion-once.py').read_bytes()).hexdigest()=='3a20b2ca78e5cd6a6fe3bdd1f31654e2f701a7436fd9bd1019b7101933d9dc75'
print(json.dumps({'motion_once_sha256':'3a20b2ca78e5cd6a6fe3bdd1f31654e2f701a7436fd9bd1019b7101933d9dc75','marker_unconsumed':True,'change':'operator prompt only'}))
