"""Recheck immutable evidence and the user's bounded right-negative acceptance revision offline."""
import contextlib
import hashlib
import importlib.util
import io
import json
import tempfile
from pathlib import Path

BASE=Path(__file__).resolve().parent
ORIGINAL=BASE.parent/'right_negative_a1'


def main():
    """Replay the historical failure auditor in scratch and keep its original verdict unchanged."""
    accepted=json.loads((BASE/'acceptance.json').read_text())
    for name,expected in accepted['original_evidence_sha256'].items():
        assert hashlib.sha256((ORIGINAL/name).read_bytes()).hexdigest()==expected,name
    entries=(ORIGINAL/'SHA256SUMS').read_text().splitlines()
    for line in entries:
        expected,name=line.split('  ',1)
        assert hashlib.sha256((ORIGINAL/name).read_bytes()).hexdigest()==expected,name
    spec=importlib.util.spec_from_file_location('original_failure_audit',ORIGINAL/'audit-failure.py')
    audit=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    with tempfile.TemporaryDirectory(prefix='g3-transient-review-') as folder:
        audit.BASE=Path(folder)
        for name in ('physical-once','jcan','operator-off.json'):
            (audit.BASE/name).symlink_to((ORIGINAL/name).resolve())
        with contextlib.redirect_stdout(io.StringIO()):
            audit.main()
        reviewed=json.loads((audit.BASE/'failure-analysis.json').read_text())
    assert reviewed==json.loads((ORIGINAL/'failure-analysis.json').read_text())
    assert reviewed['status']=='FAILED_FEEDBACK_CRITERION'
    assert len(reviewed['violations'])==1 and reviewed['violations'][0]['right_tenths']==29
    assert 0<reviewed['violations'][0]['after_zero_ms']<reviewed['stop_verified_ms']<=1000
    assert all(abs(v)<=75 for v in reviewed['raw_feedback_range_tenths_rpm'][1])
    assert reviewed['raw_feedback_range_tenths_rpm'][0]==[0,0]
    assert reviewed['no_positive_target'] and reviewed['no_restart'] and reviewed['can_counters_unchanged']
    assert reviewed['protocol']['baseline_restored'] and reviewed['protocol']['matching_frames']==1466
    assert reviewed['summary']['primary_ok']=='0' and reviewed['summary']['restore_ok']=='1'
    assert accepted['classification']=='NORMAL_START_STOP_FEEDBACK_TRANSIENT'
    assert accepted['status']=='PASS_USER_APPROVED_START_STOP_TRANSIENT'
    result=dict(status=accepted['status'],classification=accepted['classification'],
                original_verdict_preserved=reviewed['status'],archived_checksums_verified=len(entries),
                matching_frames=1466,motion_ms=reviewed['motion_ms'],
                stop_verified_ms=reviewed['stop_verified_ms'],peak_rpm=2.9,
                hardware_run=False,runtime_binary_changed=False)
    (BASE/'verification.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    main()
