#!/usr/bin/env python3
"""Exercise the receive-only observer using isolated PTYs and bounded subprocesses."""

import os
import select
import signal
import subprocess
import sys
import time


def run(binary, args, expected):
    """Check an invocation's exact exit status with a wall-clock bound."""
    result = subprocess.run([binary, *args], capture_output=True, timeout=3, check=False)
    assert result.returncode == expected, (args, result.returncode, result.stdout, result.stderr)
    return result.stdout


def start(binary, path, duration_ms=2000):
    """Start explicit 8N2 diagnostic mode and wait for its configured start record."""
    process = subprocess.Popen(
        [binary, "--device", path, "--parity", "none", "--duration-ms", str(duration_ms)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert select.select([process.stdout], [], [], 2)[0], "missing start record"
    line = process.stdout.readline()
    assert b"baud=100000 data_bits=8 parity=none stop_bits=2 parmrk=1" in line, line
    return process


def check_capture_budget(binary, master, path):
    """Cross 4096 reads, accept exactly 1 MiB, then reject the next byte."""
    process = start(binary, path, duration_ms=10000)
    try:
        remaining = 1024 * 1024
        reads = 0
        while remaining:
            size = min(250, remaining)
            os.write(master, bytes([1]) * size)
            assert select.select([process.stdout], [], [], 2)[0], "capture stalled"
            line = process.stdout.readline()
            assert b"event=read " in line and (b"kernel_raw=" + b"01" * size) in line, (reads, line)
            remaining -= size
            reads += 1
        assert reads > 4096
        os.write(master, bytes([1]))
        output, errors = process.communicate(timeout=1)
        assert process.returncode == 1 and b'operation="capture byte limit"' in output, (output, errors)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()


def main(binary):
    """Check arguments, strict format rejection, frames, SIGTERM and output failure."""
    assert b"Usage:" in run(binary, ["--help"], 0)
    for args in (
        [], ["--device"], ["--unknown", "x"], ["--device", ""],
        ["--device", "--duration-ms"],
        ["--device", "unused", "--device", "duplicate"],
        ["--device", "unused", "--parity", "odd"],
        ["--device", "unused", "--parity", "none", "--parity", "even"],
        ["--device", "unused", "--duration-ms", "1", "--duration-ms", "2"],
    ):
        run(binary, args, 2)
    for value in ("0", "-1", "60001", "1x", "abc", "9999999999999999999999"):
        run(binary, ["--device", "unused", "--duration-ms", value], 2)
    assert b"event=error" in run(binary, ["--device", "/missing/sbus"], 1)
    master, slave = os.openpty()
    path = os.ttyname(slave)
    try:
        # PTY success cannot masquerade as physical 8E2 support.
        assert b"event=error" in run(binary, ["--device", path], 1)
        output = run(binary, ["--device", path, "--parity", "none", "--duration-ms", "40"], 0)
        assert b"reason=deadline frames=0" in output, output
        process = start(binary, path)
        try:
            frames = b"".join(bytes([15]) + bytes(22) + bytes([flag, 0]) for flag in (4, 8, 0))
            os.write(master, frames)
            assert select.select([process.stdout], [], [], 1)[0], "no receive output"
            first = process.stdout.readline()
            assert b"kernel_raw=" in first, first
            for flag in (4, 8, 0):
                line = process.stdout.readline()
                assert b"event=frame" in line and f"flags={flag} ".encode() in line, line
            before = time.monotonic()
            process.send_signal(signal.SIGTERM)
            output, errors = process.communicate(timeout=1)
            assert time.monotonic() - before < 0.5
            assert process.returncode == 143 and b"reason=signal signal=15 frames=3" in output, (output, errors)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
        check_capture_budget(binary, master, path)
        process = start(binary, path)
        try:
            os.close(master)
            master = -1
            output, errors = process.communicate(timeout=1)
            assert process.returncode == 1 and b"event=error" in output, (output, errors)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    finally:
        if master >= 0:
            os.close(master)
        os.close(slave)

    # A full capture pipe must fail promptly, not hang on a blocking stdout write.
    read_fd, write_fd = os.pipe()
    try:
        os.set_blocking(write_fd, False)
        while True:
            try:
                os.write(write_fd, bytes(4096))
            except BlockingIOError:
                break
        result = subprocess.run([binary, "--help"], stdout=write_fd, timeout=1, check=False)
        assert result.returncode == 1
    finally:
        os.close(read_fd)
        os.close(write_fd)
    print("SBUS observer CLI, PTY, SIGTERM and output-backpressure tests passed")


if __name__ == "__main__":
    main(sys.argv[1])
