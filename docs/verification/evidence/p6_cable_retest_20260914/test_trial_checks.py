"""Test historical error-frame parsing and exact diagnostic acceptance offline."""
from pathlib import Path
from trial_checks import EXPECTED_INHIBITION, check_diagnostic, parse_capture, completed_lines, classify_outcome

history = Path(__file__).parent.parent / 'p6_cable_loss_20260914/target/rk3588_can.log'
records = parse_capture(history.read_text())
assert len(records) == 147
errors = [payload.hex() for ident, payload in records if ident & 0x20000000]
assert errors == ['0008000000006000', '0008000000008000']
check_diagnostic(EXPECTED_INHIBITION)
for line in ('(CO_CANsend) OS error Permission denied', EXPECTED_INHIBITION + ' warning', 'cleanup_failed=timeout'):
    try:
        check_diagnostic(line)
    except AssertionError:
        pass
    else:
        raise AssertionError('unexpected diagnostic accepted')
for text in (' (1.0) can0 601 [8] 40 00\n', ' malformed\n', ' (1.0) can0 181 [1] 00 ERRORFRAME\n'):
    try:
        parse_capture(text)
    except AssertionError:
        pass
    else:
        raise AssertionError('malformed capture accepted')
assert parse_capture(' (1.0) can0 181 [1] 00') == []
assert completed_lines('one\nqualification_external_bus_error:') == ['one']
assert classify_outcome(1, [EXPECTED_INHIBITION]) == 'INHIBITED_POWER_OFF_REQUIRED'
assert classify_outcome(0, ['qualification_complete node=1 operation=12']) == 'PROTOCOL_RECOVERY_VERIFIED'
for code, lines in ((0, [EXPECTED_INHIBITION]), (1, []), (1, [EXPECTED_INHIBITION, 'warning: unexpected'])):
    try:
        classify_outcome(code, lines)
    except AssertionError:
        pass
    else:
        raise AssertionError('invalid outcome accepted')
print('PASS: 147 historical records, both raw errors retained; exact diagnostic and malformed-input checks')
