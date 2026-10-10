import json
import os
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parent
results = {}
for variant in ('debug', 'san'):
    xml = root / f'{variant}.xml'
    command = ['ctest', '--test-dir', f'out/build/test-app-{variant}', '--output-on-failure',
               '--no-tests=error', '-V', '--output-junit', str(xml)]
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    with (root / f'{variant}.log').open('w') as stream:
        done = subprocess.run(command, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=500)
    cases = list(ET.parse(xml).iter('testcase'))
    skipped = [c.attrib['name'] for c in cases if c.find('skipped') is not None]
    failed = [c.attrib['name'] for c in cases if c.find('failure') is not None or c.find('error') is not None]
    required = {'device_config_vcan_managed', 'test_app_vcan_managed', 'canopen_vcan_managed', 'socketcan_vcan_managed'}
    passed = done.returncode == 0 and not skipped and not failed and required <= {c.attrib['name'] for c in cases}
    results[variant] = dict(passed=passed, tests=len(cases), skipped=skipped, failed=failed,
                            returncode=done.returncode, command=command)
    (root / 'local-results.json').write_text(json.dumps(results, indent=2) + '\n')
    if not passed:
        raise SystemExit(1)
