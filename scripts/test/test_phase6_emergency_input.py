#!/usr/bin/env python3
"""Test moving X1 evidence checks without CAN hardware."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / "docs/verification/evidence/p6_emergency_input_moving_20260915/trial_checks.py"
spec = importlib.util.spec_from_file_location("phase6_emergency_checks", MODULE)
checks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checks)


def expect_failure(call):
    """Require one validation callable to raise AssertionError."""
    try:
        call()
    except (AssertionError, StopIteration):
        return
    raise AssertionError("validation unexpectedly passed")


def tpdo(stamp, status, left=0, right=0):
    """Build one packed TPDO1 record."""
    payload = status.to_bytes(4, "little")
    payload += left.to_bytes(2, "little", signed=True)
    payload += right.to_bytes(2, "little", signed=True)
    return stamp, 0x181, payload


def fixture():
    """Build one accepted stop-before-target-zero and no-restart sequence."""
    inactive = 0x14601460
    active = 0x14609460
    records = [(1.0, 0x601, checks.NONZERO_TARGET)]
    records += [tpdo(1.10, inactive, right=5), tpdo(1.20, inactive, right=5),
                tpdo(1.30, active, right=4), tpdo(1.40, active), tpdo(1.50, active), tpdo(1.60, active)]
    records += [(2.0, 0x601, checks.ZERO_RIGHT_TARGET)]
    records += [tpdo(2.10 + item * 0.10, active) for item in range(9)]
    records += [tpdo(3.00 + item * 0.10, active) for item in range(5)]
    records += [tpdo(4.00 + item * 0.10, inactive) for item in range(5)]
    return records


def main():
    """Exercise stop timing, axis isolation, reset, request, and error checks."""
    records = fixture()
    result = checks.validate_records(records)
    assert result["tpdo1_frames"] == 25
    locked = [record for record in records if record[0] < 4.0]
    assert checks.validate_locked_state(locked, 3.0)["stable_zero_at"] == 1.4

    left_motion = list(records)
    left_motion[2] = tpdo(1.20, 0x14601460, left=1, right=5)
    expect_failure(lambda: checks.validate_records(left_motion))

    high_active = list(records)
    high_active[3] = tpdo(1.30, 0x94609460, right=4)
    expect_failure(lambda: checks.validate_records(high_active))

    early_zero_target = [(1.35, 0x601, checks.ZERO_RIGHT_TARGET) if item[1] == 0x601 and item[2] == checks.ZERO_RIGHT_TARGET else item
                         for item in records]
    expect_failure(lambda: checks.validate_records(early_zero_target))

    restart = [*records, tpdo(5.0, 0x14601460, right=1)]
    expect_failure(lambda: checks.validate_records(restart))
    expect_failure(lambda: checks.validate_records([*records, (5.1, 0x601, bytes.fromhex("2340600000000000"))]))
    expect_failure(lambda: checks.validate_records([*records, (5.2, 0x20000004, bytes())]))
    print("PASS: moving X1 stop, isolation, reset, request, and error checks")


if __name__ == "__main__":
    main()
