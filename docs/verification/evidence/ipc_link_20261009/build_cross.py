import hashlib
import json
import os
from pathlib import Path
import subprocess

root = Path.cwd()
out = root / 'out/verification/ipc-verification-20261009'
source = root / 'out/ipc-verification-20261009-final-source'
build = 'out/build/cross/ipc-verification-20261009-final'
sysroot = root / 'sysroots/rk3588-ubuntu2204'
lock = root / 'sysroots/rk3588-ubuntu2204.lock.json'
image = next(line.split('=', 1)[1] for line in (root/'docker/cross/image.lock').read_text().splitlines()
             if line.startswith('image_id='))
subprocess.run(['scripts/sysroot/validate_sysroot.sh', str(sysroot), str(lock)], check=True)
subprocess.run(['scripts/build/verify_cross_image.sh', image, str(root)], check=True)
subprocess.run(['scripts/build/create_source_snapshot.sh', str(source), str(out/'source.json')], check=True)
base = ['docker', 'run', '--rm', '--user', f'{os.getuid()}:{os.getgid()}', '--read-only', '--network', 'none',
        '--cap-drop', 'ALL', '--security-opt', 'no-new-privileges', '--tmpfs', '/tmp:rw,nosuid,nodev',
        '--env', 'CCACHE_DISABLE=1', '--env', 'ROBOT_CONTROL_SYSROOT=/opt/robot-control/sysroot',
        '--mount', f'type=bind,source={source},target=/workspace,readonly',
        '--mount', f'type=bind,source={root}/out,target=/workspace/out',
        '--mount', f'type=bind,source={sysroot},target=/opt/robot-control/sysroot,readonly',
        '--workdir', '/workspace', image]
configure = ['cmake', '-S', '.', '-B', build, '-G', 'Ninja',
             '-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-rk3588-ubuntu2204.cmake',
             '-DCMAKE_BUILD_TYPE=Debug', '-DBUILD_TESTING=ON', '-DROBOT_CONTROL_BUILD_TEST_APP=ON']
subprocess.run(base+configure, check=True)
targets = ['robot-control-app', 'robot_control_ipc_fixture', 'robot-control-config-check']
subprocess.run(base+['cmake', '--build', build, '--parallel', '4', '--target']+targets, check=True)
paths = [root/build/'app'/name for name in targets]
for binary in paths:
    subprocess.run(['scripts/build/audit_qualification_elf.sh', str(binary), str(sysroot)], check=True)
inputs = {}
for entry in json.loads((root/build/'compile_commands.json').read_text()):
    path = Path(entry['file'])
    if str(path).startswith('/workspace/'):
        name = str(path.relative_to('/workspace'))
        inputs[name] = hashlib.sha256((source/name).read_bytes()).hexdigest()
for directory in ('app','application','communication','domain','input','platform','service'):
    for suffix in ('*.hpp','*.h'):
        for header in (source/directory).rglob(suffix):
            inputs[str(header.relative_to(source))] = hashlib.sha256(header.read_bytes()).hexdigest()
(out/'SOURCE_INPUTS.json').write_text(json.dumps(inputs, indent=2, sort_keys=True)+'\n')
(out/'BUILD.json').write_text(json.dumps(dict(image_id=image, configure=configure,
    source=json.loads((out/'source.json').read_text()),
    sysroot_lock_sha256=hashlib.sha256(lock.read_bytes()).hexdigest(),
    artifacts={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}), indent=2)+'\n')
print('CROSS_BUILD_AND_ELF_PASS')
