"""One-time P9.3 no-device contract ABI and linkage verification against the target sysroot."""
from pathlib import Path
import re
import subprocess

binary = Path('out/build/p93-cross-smoke/robot_control_sbus_source_tests')
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
for method in ('sbus::Source::consume(', 'sbus::Source::tick(', 'sbus::Source::snapshot('):
    assert method in symbols, method
assert not re.search(r'CanSocket|CANopen|uart::SerialPort|sbus::Reader::|\b(socket|send|sendto|sendmsg|CO_[A-Za-z_]+)(@|\s|$)', symbols)
print('PASS: aarch64, interpreter, dependencies, GLIBC/GLIBCXX/CXXABI, no RPATH')
print('PASS: source linked; no UART/Reader/CANopen/CanSocket/socket/send symbols')
print('needed=' + ' '.join(needed))
