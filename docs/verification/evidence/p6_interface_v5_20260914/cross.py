"""Build fresh qualification and default Debug/Release variants inside the pinned cross runner."""
import os
import subprocess
from pathlib import Path

for variant in ('qualification', 'debug', 'release'):
    build = '/workspace/out/build/cross/interface-v5-' + variant
    assert not Path(build).exists(), 'Require a fresh build directory: ' + build
    source = '/workspace/out/hil/interface-v5' if variant == 'qualification' else '/workspace'
    kind = 'Release' if variant == 'release' else 'Debug'
    subprocess.run(['cmake', '-S', source, '-B', build, '-G', 'Ninja',
                    '-DCMAKE_BUILD_TYPE=' + kind, '-DBUILD_TESTING=OFF',
                    '-DROBOT_CONTROL_WARNINGS_AS_ERRORS=ON',
                    '-DCMAKE_TOOLCHAIN_FILE=/workspace/cmake/toolchains/aarch64-rk3588-ubuntu2204.cmake'], check=True)
    subprocess.run(['cmake', '--build', build, '--parallel', '4'], check=True)
    binaries = ['robot-control/tools/zlac_qualification/robot-control-zlac-qualification',
                'review_qualification_vcan_tests'] if variant == 'qualification' else ['tools/platform_probe/robot-control-platform-probe']
    audit = 'audit_qualification_elf.sh' if variant == 'qualification' else 'audit_elf.sh'
    for binary in binaries:
        subprocess.run(['/workspace/scripts/build/' + audit, build + '/' + binary,
                        os.environ['ROBOT_CONTROL_SYSROOT']], check=True)
