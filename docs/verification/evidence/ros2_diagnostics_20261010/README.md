# ROS2 diagnostics/package slice — 2026-10-10

PASS within device-free diagnostic scope. The user requested minimal ROS packaging/launch/run,
connection/expiry display, and a reviewable state-age/external-authority contract.
The motion v2 contract is documentation only; no external samples/challenges/authority are implemented.

Final Debug and ASan/UBSan each pass 3 focused checks. Pinned Humble passes original DDS regression,
root-CMake installed ros2 run and colcon-installed ros2 launch from a non-source cwd, with missing
socket, namespace/profile, paused /clock, 500ms silence, same-server recovery, bridge/server restart
and SIGTERM. Cross configure, fixture/ELF and Python-only ROS-component install pass without a ROS SDK.
Final installed script/launch bytes were compared with the authoritative source after the cross build.

LSP: five Python files clean; two CMake probes unavailable, replaced by actual configure/install checks.
Ruff, CI scope routing, actionlint, Bash syntax and diff checks pass. Full suite/managed vcan were
not rerun for this Python-only behavior slice; prior acceptance is preserved separately.

Initial noexec installation failure and colcon nounset setup failure remain in compressed logs.
The first was confirmed by executable-access and mount-option checks; test tmpfs was made executable.
The second was fixed by disabling nounset only around generated setup. Source/test executable bits
were corrected after Ruff EXE001 findings; Ruff recheck passes.

No new board execution/deployment, physical device/CAN/UART access, firewall change, motion, persistent
parameter change, remote Actions, commit or push accompanies this slice. Receipt freshness does not
prove source age or authenticated publisher identity. Source age remains explicitly unknown.

Commands/details: [verification baseline](../../ROS2_DIAGNOSTICS_BASELINE.md),
[usage](../../../development/ROS2_DIAGNOSTICS.md),
[authority contract](../../../plans/ROS2_EXTERNAL_AUTHORITY_CONTRACT.md).
FILES.json records final relevant source hashes; cross/source.json records the earlier immutable
cross-build snapshot. SHA256SUMS covers every archived file except the manifest itself.
