"""Reproduce timing and raw TPDO/SDO comparisons from four retained captures."""
import json
from pathlib import Path
from collections import Counter
from statistics import median
root = Path(__file__).resolve().parents[1]
sources = {"left": "p6_review_motion_20260911/left/target/rk3588_can.log", "right": "p6_review_motion_20260911/right/target/rk3588_can.log", "manual": "p6_review_rk3588_executor_20260910/hil_trial_2/target/rk3588_can.log"}
results = {}
sources["watchdog4"] = "p6_6_20260910_watchdog_trial_4/target/rk3588_can.log"
for name, source in sources.items():
    frames=[]
    for n,line in enumerate((root/source).read_text().splitlines(),1):
        parts=line.split()
        payload=bytes.fromhex("".join(parts[4:]))
        assert len(payload)==int(parts[3].strip("[]"))
        frames.append((float(parts[0].strip("()")),int(parts[2],16),payload,n))
    tpdos=[f for f in frames if f[1]==0x181]
    target=[f for f in frames if f[1]==0x601 and f[2][:3]==bytes.fromhex("23 FF 60") and any(f[2][4:])]
    start=target[0][0] if target else frames[0][0]
    zeros=[f for f in frames if f[0]>start and f[1]==0x601 and f[2][:3]==bytes.fromhex("23 FF 60") and not any(f[2][4:])]
    stop=zeros[0][0] if target else frames[-1][0]
    reads=[f for f in frames if f[1]==0x581 and f[2][1:3]==bytes.fromhex("6C 60") and f[2][0]!=0x60]
    active_reads=[f for f in reads if start<f[0]<stop]
    nonzero_reads=[f for f in reads if any(f[2][4:])]
    active=[f for f in tpdos if start<f[0]<stop]
    changes=[b for a,b in zip(tpdos,tpdos[1:]) if a[2][4:]!=b[2][4:]]
    downloads=[{"line":n,"index":hex(int.from_bytes(v[1:3],"little")),"sub":v[3],"value":int.from_bytes(v[4:],"little")} for t,i,v,n in frames if i==0x601 and v[0]!=0x40]
    packed_reads=[f for f in reads if f[2][3]==3]
    result={"source":source,"tpdo_count":len(tpdos),"tpdo_period_median_ms":round(median((b[0]-a[0])*1000 for a,b in zip(tpdos,tpdos[1:])),3),"tpdo_speed_changes":len(changes),"tpdo_status_values":dict(Counter(f[2][:4].hex(" ") for f in tpdos)),"tpdo_speed_nonzero":sum(any(f[2][4:]) for f in tpdos),"active_tpdo_count":len(active),"active_speed_sdo_responses":len(active_reads),"packed_sdo_total":len(packed_reads),"packed_sdo_nonzero":sum(any(f[2][4:]) for f in packed_reads),"downloads":downloads}
    if target:
        result["first_nonzero_sdo_after_zero_ms"]=round((nonzero_reads[0][0]-stop)*1000,3) if nonzero_reads else None
        result["nonzero_sdo_details"]=[{"line":n,"after_zero_ms":round((t-stop)*1000,3),"sub":v[3],"raw":int.from_bytes(v[4:],"little",signed=True)} for t,i,v,n in nonzero_reads]
        result["nonzero_tpdo_during_command"]=sum(any(f[2][4:]) for f in active)
    else:
        result["nonzero_small_tpdo_examples"]=[{"line":n,"low":int.from_bytes(v[4:6],"little",signed=True),"high":int.from_bytes(v[6:8],"little",signed=True)} for t,i,v,n in tpdos if 0<max(abs(int.from_bytes(v[4:6],"little",signed=True)),abs(int.from_bytes(v[6:8],"little",signed=True)))<50][:12]
    results[name]=result
print(json.dumps(results,indent=2))
(root/"p6_review_motion_20260911/tpdo_diagnosis.json").write_text(json.dumps(results,indent=2)+chr(10))
