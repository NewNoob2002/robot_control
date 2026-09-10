#!/usr/bin/env python3
"""Ensure the qualification ELF audit rejects either embedded runtime search path."""
from pathlib import Path
import os
import subprocess
import tempfile


def main():
    """Exercise the audit with deterministic readelf/file responses and no target access."""
    audit = Path(__file__).resolve().parents[1] / 'build/audit_qualification_elf.sh'
    with tempfile.TemporaryDirectory(prefix='qualification-elf-audit-') as directory:
        root = Path(directory)
        (root / 'sysroot').mkdir()
        scripts = {
            'file': '#!/bin/sh\necho "ELF 64-bit ARM aarch64"\n',
            'aarch64-linux-gnu-readelf': '''#!/bin/sh
case "$1" in
  --program-headers) echo 'Requesting program interpreter: /lib/ld-linux-aarch64.so.1' ;;
  --dynamic) cat "$2" ;;
  --version-info) : ;;
  *) exit 2 ;;
esac
'''}
        for name, content in scripts.items():
            path = root / name
            path.write_text(content)
            path.chmod(0o700)
        for tag in ('none', 'RPATH', 'RUNPATH'):
            binary = root / (tag + '.elf')
            binary.write_text('' if tag == 'none' else f'0x000000000000001d ({tag}) Library path: [/untrusted]\n')
            result = subprocess.run(['bash', str(audit), str(binary), str(root / 'sysroot')],
                                    env={**os.environ, 'PATH': str(root) + os.pathsep + os.environ['PATH']},
                                    capture_output=True, text=True, timeout=5)
            assert (result.returncode == 0) == (tag == 'none'), (tag, result.returncode, result.stdout, result.stderr)
    print('PASS: qualification ELF audit accepts no runtime path and rejects RPATH/RUNPATH')


if __name__ == '__main__':
    main()
