"""Reproduce the P10.3 single-wheel ControlLoop artifact cross build with the locked image and real target sysroot."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

root=Path.cwd()
sysroot=root/'sysroots/rk3588-ubuntu2204'
lock=root/'sysroots/rk3588-ubuntu2204.lock.json'
image=next(line.split('=',1)[1] for line in (root/'docker/cross/image.lock').read_text().splitlines() if line.startswith('image_id='))
snapshot=root/'out/p103-control-motion-v1-cross-source'
attestation=root/'out/p103-control-motion-v1-cross-source.json'
subprocess.run(['scripts/sysroot/validate_sysroot.sh',str(sysroot),str(lock)],check=True)
subprocess.run(['scripts/build/verify_cross_image.sh',image,str(root)],check=True)
subprocess.run(['scripts/build/create_source_snapshot.sh',str(snapshot),str(attestation)],check=True)
base=['docker','run','--rm','--user',f'{os.getuid()}:{os.getgid()}','--read-only','--network','none',
      '--cap-drop','ALL','--security-opt','no-new-privileges','--tmpfs','/tmp:rw,nosuid,nodev',
      '--env','CCACHE_DISABLE=1','--env','ROBOT_CONTROL_SYSROOT=/opt/robot-control/sysroot',
      '--mount',f'type=bind,source={snapshot},target=/workspace,readonly',
      '--mount',f'type=bind,source={root}/out,target=/workspace/out',
      '--mount',f'type=bind,source={sysroot},target=/opt/robot-control/sysroot,readonly',
      '--workdir','/workspace',image]
configure=['cmake','-S','.','-B','out/build/cross/p103-control-motion-v1-runtime','-G','Ninja',
           '-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-rk3588-ubuntu2204.cmake',
           '-DCMAKE_BUILD_TYPE=Debug','-DBUILD_TESTING=ON','-DROBOT_CONTROL_BUILD_CONTROL_HIL=ON']
subprocess.run(base+configure,check=True)
subprocess.run(base+['cmake','--build','out/build/cross/p103-control-motion-v1-runtime','--parallel','2'],check=True)
subprocess.run(['scripts/sysroot/validate_sysroot.sh',str(sysroot),str(lock)],check=True)
metadata={'source':json.loads(attestation.read_text()),'image_id':image,'configure':configure,
          'sysroot_lock_sha256':hashlib.sha256(lock.read_bytes()).hexdigest(),
          'sysroot_content':(sysroot/'.robot-control/sysroot-content.sha256').read_text().strip(),
          'artifacts':{}}
for name in ('tools/control_hil/robot-control-hil','robot_control_control_cycle_tests'):
 p=root/'out/build/cross/p103-control-motion-v1-runtime'/name
 metadata['artifacts'][name]=hashlib.sha256(p.read_bytes()).hexdigest()
(root/'out/p103-control-motion-v1-cross-metadata.json').write_text(json.dumps(metadata,indent=2)+chr(10))
