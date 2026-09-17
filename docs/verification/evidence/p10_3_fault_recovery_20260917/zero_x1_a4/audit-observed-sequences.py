"""Audit A4's two operator-confirmed intervals without rewriting its original result."""
import json
from pathlib import Path
import runpy

BASE = Path(__file__).resolve().parent
ROOT = next(p for p in BASE.parents if (p/'CMakeLists.txt').exists())
analyzer = runpy.run_path(str(ROOT/'scripts/test/analyze_control_hil_trace.py'))
text = (BASE/'recovery-target/application.log').read_text()
data = analyzer['analyze'](text)
assert not data['feedback_bad'] and not data['discontinuities']
try:
    analyzer['analyze_recovery'](text, 10)
except (AssertionError, StopIteration):
    pass
else:
    raise AssertionError('A4 mixed-event completion must be rejected')
rx = [(t,p) for t,i,p,_,_ in data['rx'] if i == 0x181]
tx = [(t,p) for t,i,p in data['tx'] if i == 0x201]
assert len(tx) == 4036 and all(p[2:] == bytes(4) for _,p in tx)
assert all(p[4:] == bytes(4) for _,p in rx)
marks = {d['phase']:int(d['at_ns']) for line in text.splitlines()
         if line.startswith('event=recovery ') for d in [analyzer['fields'](line)]}
results = []
for generation in (2,3):
    start = next(t for t,d in data['cycles'] if d[2] == generation)
    end = next((t for t,d in data['cycles'] if t > start and (d[2] != generation or not d[14])),
               data['stops'][0]['ns'])
    lower = marks['fault_observed'] if generation == 2 else results[0]['enabled_ns']
    enabled = next(t for t,p in rx if start < t < end
                   and all(int.from_bytes(p[o:o+2], 'little') & 0x6f == 0x27 for o in (0,2)))
    window = [(t,p) for t,p in rx if lower < t <= enabled]
    quick = [t for t,p in window if all(int.from_bytes(p[o:o+2], 'little') & 0x6f == 7 for o in (0,2))]
    assert bool(quick) == (generation == 3)
    if quick:
        lower = next(t for t,p in tx if max(start,quick[-1]) < t < enabled and p[0] == 0)
    disabled = next(t for t,p in window if t > lower
                    and all(int.from_bytes(p[o:o+2], 'little') & 0x4f == 0x40 for o in (0,2)))
    cursor = max(start,disabled)
    for word,state in ((6,0x21),(7,0x23),(15,0x27)):
        command = next(t for t,p in tx if cursor < t < enabled and p[0] == word)
        cursor = next(t for t,p in window if command < t <= enabled
                      and all(int.from_bytes(p[o:o+2], 'little') & 0x6f == state for o in (0,2)))
    assert cursor == enabled and end-enabled >= 1000000000
    assert all(all(int.from_bytes(p[o:o+2], 'little') & 0x6f == 0x27 for o in (0,2))
               for t,p in rx if enabled <= t <= enabled+1000000000)
    results.append({'source_generation':generation,'rearm_ns':start,'enabled_ns':enabled,
                    'path':'native_x1_disabled' if generation == 2 else 'CH6_quick_stop',
                    'ordered_transition_and_one_second_enabled_hold':True})
report = {'status':'OBSERVED_SEQUENCES_VERIFIED_ORIGINAL_COMBINED_CLAIM_SUPERSEDED',
          'sequences':results,'all_targets_zero':True,'original_combined_oracle_rejected':True,
          'scope':'Retrospective interval audit; corrected observer binary not physically tested',
          'operator_confirmation':json.loads((BASE/'operator-chat-followup.json').read_text())}
(BASE/'observed-sequences-analysis.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(report,ensure_ascii=False,indent=2))
