#!/usr/bin/env python3
"""Exercise streaming diagnostics framing, logger output and incomplete evidence rejection."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

PACKET = struct.Struct('<19q')


def packet(ordinal, kind, fields=(), ns=1000000000):
    """Encode one independent version-one fixture packet."""
    return PACKET.pack(ordinal, kind, ns, *(tuple(fields)+(0,)*(16-len(fields))))


def main():
    """Check a valid stream and independently malformed/truncated/failing sinks."""
    hil, collector = map(Path, sys.argv[1:])
    assert '--zero-soak' in subprocess.check_output([hil, '--help'], text=True)
    start = packet(0, 10, (1, 20))
    timing = packet(1, 7, (100, 0, 12, 25, 1000, 2000))
    state = packet(2, 8, (0, 1, 0x14401440, 0, 3, 0, 0, 10, 11, 12, 13, 0, 2, 1, 1, 0))
    end = packet(3, 9, (0, 0, 20))
    valid = start+timing+state+end
    with tempfile.TemporaryDirectory() as tmp:
        raw = Path(tmp)/'raw.bin'
        for data, success in ((valid, True), (valid[:-1], False), (start, False),
                              (start+packet(3, 9), False), (start+packet(1, 9, (1,)), False)):
            with raw.open('wb') as output:
                result = subprocess.run([collector], input=data, stdout=output, stderr=subprocess.PIPE, timeout=3)
            assert (result.returncode == 0) == success, result.stderr
            if success:
                assert raw.read_bytes() == valid
                assert b'event="control_timing"' in result.stderr and b'event="drive_state"' in result.stderr
        with open('/dev/full', 'wb') as output:
            result = subprocess.run([collector], input=valid, stdout=output, stderr=subprocess.PIPE, timeout=3)
            assert result.returncode != 0
        # Closed stderr and a full pipe must not hang the collector.
        read_fd, write_fd = os.pipe2(os.O_NONBLOCK)
        try:
            while True:
                os.write(write_fd, b'x'*4096)
        except BlockingIOError:
            pass
        try:
            with raw.open('wb') as output:
                result = subprocess.run([collector], input=valid, stdout=output, stderr=write_fd, timeout=3)
            assert result.returncode != 0
        finally:
            os.close(read_fd)
            os.close(write_fd)
    print('PASS: streaming diagnostics, EasyLogger, framing and failing sinks')


if __name__ == '__main__':
    main()
