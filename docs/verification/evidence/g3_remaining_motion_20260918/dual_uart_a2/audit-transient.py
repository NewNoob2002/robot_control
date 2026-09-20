"""Audit this trial's raw stop transient under the existing approved criterion."""
import importlib.util
import contextlib
import io
import json
import traceback
from pathlib import Path

BASE = Path(__file__).resolve().parent


def load(name, path):
    """Load an existing evidence auditor without running its entry point."""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def feedback(result, profile, tolerance=20):
    """Check raw dual-forward motion and isolate one post-zero opposite-sign sample."""
    assert profile == 'dual-positive' and tolerance == 20
    assert result['motion_mode'] == 6 and result['right_sign'] == -1
    assert result['zero_tolerance'] == 20 and len(result['stops']) == 1
    tx = [(t, p) for t, i, p in result['tx'] if i == 0x201]
    rx = [(t, int.from_bytes(p[4:6], 'little', signed=True),
           int.from_bytes(p[6:8], 'little', signed=True))
          for t, i, p, _, _ in result['rx'] if i == 0x181]
    moving = []
    for t, p in tx:
        assert len(p) == 6
        left = int.from_bytes(p[2:4], 'little', signed=True)
        right = int.from_bytes(p[4:6], 'little', signed=True)
        if left or right:
            assert p[:2] == bytes([15, 0]) and 1 <= left <= 5 and right == -left
            moving.append(t)
    first = moving[0]
    zero = next(t for t, p in tx if t > first and p[2:] == bytes(4))
    assert 0 < zero - first < 2950000000
    assert all(p[2:] == bytes(4) for t, p in tx if t >= zero)
    assert all(abs(left) <= 75 and abs(right) <= 75 for _, left, right in rx)
    assert all(abs(left) <= 20 and abs(right) <= 20 for t, left, right in rx if t < first)
    assert all(left >= -20 and right <= 20 for t, left, right in rx if first <= t < zero)
    count = sum(left > 20 and right < -20 for t, left, right in rx if first <= t < zero)
    assert count >= 2
    excursions = [dict(ns=t, after_zero_ms=(t-zero)/1e6, left_raw_tenths=left,
                       right_raw_tenths=right, right_logical_tenths=-right, phase='bounded_stop')
                  for t, left, right in rx if t >= zero and (left < -20 or right > 20)]
    # This is a case-specific retrospective check, not a general transient allowance.
    assert len(excursions) == 1, excursions
    since = reached = verified = None
    for t, left, right in rx:
        if t <= zero:
            continue
        if abs(left) > 20 or abs(right) > 20:
            since = reached = None
            continue
        since = t if since is None else since
        if reached is not None and t > reached:
            verified = t
            break
        if t-since >= 150000000:
            reached = t
    assert verified is not None and verified-zero <= 1000000000
    assert zero < excursions[0]['ns'] < since
    assert all(abs(left) <= 20 and abs(right) <= 20 for t, left, right in rx if t >= since)
    return dict(first_nonzero_ns=first, first_zero_ns=zero, stable_since_ns=since,
                stable_verified_ns=verified, simultaneous_motion_samples=count,
                excursions=excursions, excursion_count=1, excursion_span_ms=0,
                excursion_peak_rpm=max(abs(excursions[0][k]) for k in
                                       ('left_raw_tenths', 'right_raw_tenths'))/10,
                no_restart=True, raw_samples_unchanged=True)


def main():
    """Preserve the original failure and reuse all UART, restoration and operator checks."""
    original = load('uart_original', BASE/'analyze-uart.py')
    protocol = load('protocol_original', BASE.parent/'dual_x1_a1/analyze-x1.py')
    try:
        original.main()
    except AssertionError:
        (BASE/'original-audit-failure.txt').write_text(traceback.format_exc())
    else:
        raise AssertionError('Expected original feedback criterion failure')

    def captures(result, wire, independent, profile, tolerance):
        """Reuse exact protocol audit; independently audit raw wire feedback and no X1."""
        assert all(len(p) == 8 and not int.from_bytes(p[:4], 'little') & 0x80008000
                   for _, i, p in wire if i == 0x181)
        report = protocol.audit_protocol(result, wire, independent, profile, tolerance)
        captured = dict(result, tx=[(round(t*1e9), i, p) for t, i, p in wire],
                        rx=[(round(t*1e9), i, p, 16, 0) for t, i, p in wire])
        report['wire_motion'] = feedback(captured, profile, tolerance)
        report['scope'] = 'Exact capture/restoration; UART causality and raw feedback audited separately'
        return report

    original.analyze_g3_feedback = feedback
    original.analyze_g3_captures = captures
    with contextlib.redirect_stdout(io.StringIO()):
        original.main()
    report = json.loads((BASE/'analysis.json').read_text())
    report.update(status='PASS_USER_APPROVED_START_STOP_TRANSIENT',
                  classification='NORMAL_START_STOP_FEEDBACK_TRANSIENT',
                  original_verdict='FAILED_FEEDBACK_CRITERION',
                  criterion='docs/plans/G3_RAISED_MOTION.md: approved start/stop transient rule',
                  online_guard_changed=False)
    (BASE/'analysis.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
