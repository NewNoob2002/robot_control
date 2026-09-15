"""Strict CAN capture checks shared by the X1 emergency-input trial."""
import re


def parse_capture(text):
    """Parse complete candump records while retaining CAN error frames."""
    records = []
    previous_error = False
    for line in text.splitlines(keepends=True):
        if not line.endswith("\n"):
            break
        match = re.fullmatch(
            r"\s*\(([0-9]+\.[0-9]+)\)\s+can0\s+([0-9A-Fa-f]+)\s+\[([0-8])\]\s*(.*?)\s*",
            line,
        )
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


def status_bit15_passes(values):
    """Return true when each packed status half observes an inactive-active-inactive sequence."""
    for shift in (0, 16):
        states = [bool((value >> shift) & 0x8000) for value in values]
        try:
            active = states.index(True)
        except ValueError:
            return False
        if not any(not state for state in states[:active]) or not any(not state for state in states[active + 1:]):
            return False
    return True


def validate_records(records):
    """Validate zero motion, allowed requests, and the external-stop status transition."""
    assert records and not any(ident & 0x20000000 for _, ident, _ in records), "CAN error frame"
    statuses = []
    tpdo_count = 0
    for _, ident, payload in records:
        assert ident in (0, 0x181, 0x281, 0x381, 0x481, 0x581, 0x601, 0x701), hex(ident)
        if ident == 0:
            assert payload in (bytes([0x80, 1]), bytes([1, 1])), payload.hex()
        elif ident == 0x181:
            assert len(payload) == 8 and payload[4:] == bytes(4), payload.hex()
            statuses.append(int.from_bytes(payload[:4], "little"))
            tpdo_count += 1
        elif ident in (0x281, 0x381, 0x481):
            assert len(payload) == 0, payload.hex()
        elif ident == 0x581:
            assert len(payload) == 8 and payload[0] != 0x80, payload.hex()
        elif ident == 0x601:
            assert len(payload) == 8, payload.hex()
            command = payload[0]
            index = int.from_bytes(payload[1:3], "little")
            sub = payload[3]
            if command == 0x40:
                allowed = {(0x6060, 0), (0x6061, 0), (0x60FF, 1), (0x60FF, 2), (0x603F, 0),
                           (0x6041, 0), (0x1017, 0), (0x1800, 1), (0x1800, 2), (0x1800, 5),
                           (0x1A00, 0), (0x1A00, 1), (0x1A00, 2)}
                allowed |= {(0x606C, part) for part in (1, 2, 3)}
                assert (index, sub) in allowed and payload[4:] == bytes(4), payload.hex()
            else:
                value = int.from_bytes(payload[4:], "little")
                assert (command, index, sub, value) in ((0x2B, 0x1017, 0, 500), (0x2B, 0x1017, 0, 0)), payload.hex()
    assert tpdo_count >= 20, tpdo_count
    assert status_bit15_passes(statuses), "statusword bit 15 did not show inactive-active-inactive on both halves"
    return {"frames": len(records), "tpdo1_frames": tpdo_count, "status_samples": len(statuses)}
