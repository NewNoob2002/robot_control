"""Strict traffic and stopping checks for the moving X1 trial."""
import re

ALLOWED_REQUESTS = {bytes.fromhex(value) for value in (
    "23ff600300000000", "23ff600300000500", "2b17100000000000",
    "2b171000f4010000", "2b40600000000000", "2b40600006000000",
    "2b40600007000000", "2b4060000f000000", "2f60600003000000",
    "400f200000000000", "403f600000000000", "4060600000000000",
    "4061600000000000", "406c600100000000", "406c600200000000",
    "406c600300000000", "40ff600100000000", "40ff600200000000", "40ff600300000000",
    "4000180100000000", "4000180200000000", "4000180500000000",
    "40001a0000000000", "40001a0100000000", "40001a0200000000",
    "4017100000000000", "4041600000000000")}
NONZERO_TARGET = bytes.fromhex("23ff600300000500")
ZERO_RIGHT_TARGET = bytes.fromhex("23ff600300000000")


def parse_capture(text):
    """Parse complete candump records while retaining error frames."""
    records = []
    previous_error = False
    for line in text.splitlines(keepends=True):
        if not line.endswith("\n"):
            break
        match = re.fullmatch(
            r"\s*\(([0-9]+\.[0-9]+)\)\s+can0\s+([0-9A-Fa-f]+)\s+\[([0-8])\]\s*(.*?)\s*", line)
        if match is None:
            assert previous_error and line[:1].isspace() and not line.lstrip().startswith("("), line
            continue
        timestamp, ident, dlc = float(match[1]), int(match[2], 16), int(match[3])
        tokens = match[4].split()
        assert len(tokens) >= dlc and all(re.fullmatch(r"[0-9A-Fa-f]{2}", item) for item in tokens[:dlc]), line
        previous_error = bool(ident & 0x20000000)
        assert tokens[dlc:] == (["ERRORFRAME"] if previous_error else []), line
        records.append((timestamp, ident, bytes.fromhex("".join(tokens[:dlc]))))
        assert len(records) <= 100000, "capture bound exceeded"
    return records


def _tpdo(records):
    """Return timestamped packed status and signed left/right velocities."""
    return [(stamp, int.from_bytes(payload[:4], "little"),
             int.from_bytes(payload[4:6], "little", signed=True),
             int.from_bytes(payload[6:8], "little", signed=True))
            for stamp, ident, payload in records if ident == 0x181 and len(payload) == 8]


def validate_envelope(records):
    """Reject CAN errors, malformed frames, and requests outside the reviewed union."""
    assert records and not any(ident & 0x20000000 for _, ident, _ in records), "CAN error frame"
    requests = []
    for _, ident, payload in records:
        assert ident in (0, 0x181, 0x281, 0x381, 0x481, 0x581, 0x601, 0x701), hex(ident)
        if ident == 0:
            assert payload in (bytes([1, 1]), bytes([0x80, 1])), payload.hex()
        elif ident == 0x181:
            assert len(payload) == 8, payload.hex()
        elif ident in (0x281, 0x381, 0x481):
            assert not payload, payload.hex()
        elif ident == 0x581:
            assert len(payload) == 8 and payload[0] != 0x80, payload.hex()
        elif ident == 0x601:
            assert payload in ALLOWED_REQUESTS, payload.hex()
            requests.append(payload)
        elif ident == 0x701:
            assert payload in (b"\x00", b"\x05", b"\x7f"), payload.hex()
    assert requests.count(NONZERO_TARGET) == 1, requests.count(NONZERO_TARGET)
    return requests


def _moving_stop(records):
    """Establish X1 stopping while the application target was still nonzero."""
    requests = validate_envelope(records)
    samples = _tpdo(records)
    assert len(samples) >= 20 and all(left == 0 for _, _, left, _ in samples), "left wheel moved"
    target_on = next(stamp for stamp, ident, payload in records if ident == 0x601 and payload == NONZERO_TARGET)
    target_off = next(stamp for stamp, ident, payload in records
                      if stamp > target_on and ident == 0x601 and payload == ZERO_RIGHT_TARGET)
    moving = [sample for sample in samples if target_on <= sample[0] < target_off and sample[3] != 0]
    assert moving, "right-wheel motion feedback absent"
    first_motion = moving[0][0]
    active = next((sample for sample in samples if sample[0] > first_motion and sample[1] & 0x8000), None)
    assert active is not None and not active[1] & 0x80000000, "X1 low-half activation absent"
    assert all(not status & 0x80000000 for _, status, _, _ in samples), "unexpected high-half X1 status"
    after_active = [sample for sample in samples if active[0] <= sample[0] < target_off]
    stable_zero = next((after_active[index][0] for index in range(max(0, len(after_active) - 2))
                        if all(sample[3] == 0 for sample in after_active[index:index + 3])), None)
    assert stable_zero is not None and stable_zero < target_off, "right wheel did not stop before target zero"
    assert not any(right for stamp, _, _, right in samples if stamp >= stable_zero), "right wheel restarted"
    return samples, requests, target_on, target_off, first_motion, active[0], stable_zero


def validate_locked_state(records, manual_started_at):
    """Require stopped feedback and active X1 after the disabled observer starts."""
    samples, _, _, target_off, _, active, stable_zero = _moving_stop(records)
    locked = [sample for sample in samples if sample[0] >= manual_started_at]
    assert locked and all((status & 0x8000) and not (status & 0x80000000) and left == 0 and right == 0
                          for _, status, left, right in locked), "locked observer did not remain stopped"
    return {"x1_active_at": active, "stable_zero_at": stable_zero, "target_zero_at": target_off}


def validate_records(records):
    """Validate moving stop, locked zero, reset, and no restart."""
    samples, requests, target_on, target_off, first_motion, active, stable_zero = _moving_stop(records)
    reset = next((stamp for stamp, status, _, _ in samples if stamp > target_off and not status & 0x8000), None)
    assert reset is not None and any(stamp < active and not status & 0x8000 for stamp, status, _, _ in samples)
    assert any(target_off < stamp < reset and status & 0x8000 for stamp, status, _, _ in samples)
    assert all(left == 0 and right == 0 for stamp, _, left, right in samples if stamp >= reset), "motion after X1 reset"
    return {"frames": len(records), "tpdo1_frames": len(samples), "sdo_requests": len(requests),
            "target_to_motion_ms": (first_motion - target_on) * 1000,
            "x1_to_stable_zero_ms": (stable_zero - active) * 1000,
            "stop_before_target_zero_ms": (target_off - stable_zero) * 1000,
            "x1_active_until_reset_ms": (reset - active) * 1000}
