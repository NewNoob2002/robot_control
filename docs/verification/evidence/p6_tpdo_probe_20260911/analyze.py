"""Compare timestamped independent/packed SDO samples with two passive captures."""
import json
from pathlib import Path
out = Path(__file__).resolve().parent / "left"
frames = []
for n,line in enumerate((out/"target/rk3588_can.log").read_text().splitlines(),1):
    fields = line.split()
    data = bytes.fromhex("".join(fields[4:]))
    assert len(data) == int(fields[3].strip("[]"))
    frames.append((float(fields[0].strip("()")), int(fields[2],16), data,n))
observed = []
for line in (out/"jcan_session.jsonl").read_text().splitlines():
    event = json.loads(line)
    if event.get("event") == "frame":
        observed.append((event["can_id"],bytes.fromhex(event["data_hex"])))
assert [(i,d) for t,i,d,n in frames] == observed
commands = [f for f in frames if f[1] == 0x601 and f[2][:3] == bytes.fromhex("23 FF 60") and any(f[2][4:])]
assert len(commands) == 1 and commands[0][2] == bytes.fromhex("23 FF 60 01 05 00 00 00")
start = commands[0][0]
zero = next(f[0] for f in frames if f[0] > start and f[1] == 0x601 and f[2] == bytes.fromhex("23 FF 60 01 00 00 00 00"))
tpdos = [f for f in frames if f[1] == 0x181]
assert all(len(f[2]) == 8 for f in tpdos)
pending = None
samples = []
reads = {}
transactions = 0
for t,i,d,n in frames:
    if i == 0x601:
        assert pending is None
        pending = (t,d,n)
    elif i == 0x581:
        assert pending and pending[1][1:4] == d[1:4] and d[0] != 0x80
        transactions += 1
        if pending[1][0] == 0x40:
            key = (int.from_bytes(d[1:3],"little"),d[3])
            reads[key] = int.from_bytes(d[4:],"little")
            if start < pending[0] < zero and key[0] == 0x606c:
                samples.append({"sub":d[3],"value":int.from_bytes(d[4:],"little",signed=True),"bytes":d[4:].hex(" "),"request_ms":round((pending[0]-start)*1000,3),"response_ms":round((t-start)*1000,3),"request_line":pending[2],"response_line":n,"timestamp":t})
        else:
            assert d[0] == 0x60
        pending = None
assert pending is None
assert 0 < len(samples) <= 18 and len(samples)%3 == 0
groups = []
for offset in range(0,len(samples),3):
    group = samples[offset:offset+3]
    assert [s["sub"] for s in group] == [1,2,3]
    packed = bytes.fromhex(group[2]["bytes"])
    near = min(tpdos,key=lambda f:abs(f[0]-group[2]["timestamp"]))
    groups.append({"samples":group,"independent_raw":[group[0]["value"],group[1]["value"]],"packed_sdo_raw":[int.from_bytes(packed[:2],"little",signed=True),int.from_bytes(packed[2:],"little",signed=True)],"nearest_tpdo_raw":[int.from_bytes(near[2][4:6],"little",signed=True),int.from_bytes(near[2][6:],"little",signed=True)],"tpdo_line":near[3],"tpdo_offset_ms":round((near[0]-group[2]["timestamp"])*1000,3)})
assert all(reads[k] == 0 for k in ((0x60ff,1),(0x60ff,2),(0x606c,1),(0x606c,2),(0x606c,3),(0x603f,0),(0x1017,0)))
assert reads[(0x200f,0)] == 1
mode_writes = [(t,int.from_bytes(d[4:],"little")) for t,i,d,n in frames if i == 0x601 and d[:4] == bytes.fromhex("2B 0F 20 00")]
assert [v for t,v in mode_writes] == [0,1] and mode_writes[0][0] < start and mode_writes[1][0] > zero
assert all((int.from_bytes(tpdos[-1][2][s:s+2],"little") & 0x6f) == 0x21 for s in (0,2))
assert [d for t,i,d,n in frames if i == 0x701][-1] == bytes([0x7f])
pre = json.loads((out/"target/can_preflight.json").read_text())[0]
post = json.loads((out/"target/can_postflight.json").read_text())[0]
assert post["linkinfo"]["info_data"]["state"] == "ERROR-ACTIVE"
assert all(post["stats64"][a][b] == 0 for a in ("rx","tx") for b in ("errors","dropped"))
tx = sum(i in (0,0x601) for t,i,d,n in frames)
assert post["stats64"]["tx"]["packets"]-pre["stats64"]["tx"]["packets"] == tx
for direction in ("rx","tx"):
    selected = [f for f in frames if (f[1] in (0,0x601)) == (direction == "tx")]
    assert post["stats64"][direction]["packets"]-pre["stats64"][direction]["packets"] == len(selected)
    assert post["stats64"][direction]["bytes"]-pre["stats64"][direction]["bytes"] == sum(len(f[2]) for f in selected)
wrapper = json.loads((out/"target/wrapper_result.json").read_text())
coordinator = json.loads((out/"coordinator_result.json").read_text())
assert wrapper["wrapper_exit"] == 0 and wrapper["executor_stopped"] and wrapper["capture_stopped"] and coordinator["error"] is None
result = {"schema_version":1,"level":"hil","command":"python3 docs/verification/evidence/p6_tpdo_probe_20260911/left/local_trial.py","attempts":1,"protocol_and_cleanup_passed":True,"frames":len(frames),"tx_frames":tx,"sdo_transactions":transactions,"capture_pairs_equal":True,"command_to_zero_ms":round((zero-start)*1000,3),"running_samples":groups,"can_errors_drops":0,"final_targets_speeds_fault_heartbeat_zero":True,"application_mode_restored":1,"physical_observation":"pending","timing_note":"Sequential SDO samples and nearest TPDO are not simultaneous."}
if (out/"operator_observation.json").exists():
    result["physical_observation"] = json.loads((out/"operator_observation.json").read_text())
(out/"analysis.json").write_text(json.dumps(result,indent=2)+chr(10))
print(json.dumps(result,indent=2))
