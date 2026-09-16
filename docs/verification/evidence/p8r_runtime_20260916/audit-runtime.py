"""Audit P8-R runtime linkage and ABI without target execution."""
from pathlib import Path
import re
import subprocess

binary = Path('out/build/cross/p8r-runtime/robot_control_canopen_runtime_vcan_tests')
sysroot = Path('sysroots/rk3588-ubuntu2204')

def readelf(*args):
    """Read ELF metadata without executing target code."""
    return subprocess.check_output(['readelf', '--wide', *map(str, args)], text=True)

assert 'ARM aarch64' in subprocess.check_output(['file', str(binary)], text=True)
headers = readelf('--program-headers', binary)
assert re.search(r'Requesting program interpreter: /lib/(aarch64-linux-gnu/)?ld-linux-aarch64.so.1', headers)
dynamic = readelf('--dynamic', binary)
assert not re.search(r'\((RPATH|RUNPATH)\)', dynamic)
needed = re.findall(r'Shared library: \[([^\]]+)\]', dynamic)
for name in needed:
    assert any((sysroot / directory / name).exists() for directory in (
        'lib/aarch64-linux-gnu', 'usr/lib/aarch64-linux-gnu', 'lib', 'usr/lib')), name
versions = readelf('--version-info', binary)
for prefix, library in (
    ('GLIBC', 'lib/aarch64-linux-gnu/libc.so.6'),
    ('GLIBCXX', 'usr/lib/aarch64-linux-gnu/libstdc++.so.6'),
    ('CXXABI', 'usr/lib/aarch64-linux-gnu/libstdc++.so.6'),
):
    required = set(re.findall(prefix + r'_[0-9]+(?:\.[0-9]+)*', versions))
    available = set(re.findall(prefix + r'_[0-9]+(?:\.[0-9]+)*', readelf('--version-info', sysroot / library)))
    assert required <= available, required - available
symbols = readelf('--symbols', '--demangle', binary)
for method in ('RuntimeSession::submit(', 'RuntimeSession::stop(', 'RuntimePolicy::evaluate(', 'Lifecycle::run_until('):
    assert method in symbols, method
assert 'robot_control_canopen_deny_transmit' in symbols
assert 'robot_control_canopen_qualification_transmit' not in symbols
assert 'robot_control_canopen_commissioning_transmit' not in symbols
print('PASS: aarch64, interpreter, target dependencies/versions, no RPATH')
print('PASS: runtime policy/session/owner linked; upstream deny gate retained; P6 gate absent')
print('needed=' + ' '.join(needed))
