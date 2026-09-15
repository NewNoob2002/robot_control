"""Strict capture and terminal-diagnostic checks for the one-shot cable retest."""
import re

EXPECTED_INHIBITION = ('qualification_external_bus_error: interface=can0 remote=1 '
    'watchdog_retained=1000_or_unverified operator_power_cut_required: Input/output error')

def expected_inhibition(line):
    """Recognize only the reviewed fail-closed diagnostic."""
    return line == EXPECTED_INHIBITION

def check_diagnostic(line):
    """Reject other warnings or errors, including rejected Abort transmissions."""
    assert expected_inhibition(line) or not any(word in line.lower() for word in
        ('error', 'warning', 'failed', 'permission denied')), 'application diagnostic: ' + line

def completed_lines(text):
    """Avoid classifying a diagnostic while its writer is still completing it."""
    return text[:text.rfind('\n') + 1].splitlines()

def classify_outcome(returncode, lines):
    """Distinguish verified protocol recovery from expected manual-power inhibition."""
    for line in lines:
        check_diagnostic(line)
    if returncode == 1 and sum(expected_inhibition(line) for line in lines) == 1:
        return 'INHIBITED_POWER_OFF_REQUIRED'
    if returncode == 0 and sum(line.startswith('qualification_complete node=1') for line in lines) == 1:
        return 'PROTOCOL_RECOVERY_VERIFIED'
    raise AssertionError('unexpected external-loss outcome')

def parse_capture(text):
    """Parse complete candump records, retaining error frames and their DLC bytes."""
    records = []
    previous_error = False
    for line in text.splitlines(keepends=True):
        if not line.endswith('\n'):
            break
        match = re.fullmatch(r'\s*\([0-9]+\.[0-9]+\)\s+can0\s+([0-9A-Fa-f]+)\s+\[([0-8])\]\s*(.*?)\s*', line)
        if match is None:
            assert previous_error and line[:1].isspace() and not line.lstrip().startswith('('), line
            continue
        ident, dlc = int(match[1], 16), int(match[2])
        tokens = match[3].split()
        assert len(tokens) >= dlc and all(re.fullmatch('[0-9A-Fa-f]{2}', x) for x in tokens[:dlc]), line
        previous_error = bool(ident & 0x20000000)
        assert tokens[dlc:] == (['ERRORFRAME'] if previous_error else []), line
        records.append((ident, bytes.fromhex(''.join(tokens[:dlc]))))
        assert len(records) <= 100000, 'capture bound exceeded'
    return records
