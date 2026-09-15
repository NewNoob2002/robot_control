"""Strict parsing helpers for the one-shot userspace-inhibitor trial."""
import re


def parse_capture(text):
    """Parse complete candump records, retaining CAN error frames."""
    records = []
    previous_error = False
    for line in text.splitlines(keepends=True):
        if not line.endswith("\n"):
            break
        match = re.fullmatch(r"\s*\(([0-9]+\.[0-9]+)\)\s+can0\s+([0-9A-Fa-f]+)\s+\[([0-8])\]\s*(.*?)\s*", line)
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
