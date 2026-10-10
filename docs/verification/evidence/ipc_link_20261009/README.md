# IPC verification and board-to-host diagnostics — 2026-10-09

The current device-free diagnostic path passes over native Fast DDS TCP. Default
DDS and explicit UDP peers received no data on this routed network; their failures
remain here. Motion authority and the board-local Unix IPC contract are unchanged.

| Check | Result |
| --- | --- |
| App host Debug | 42/42 CTest cases pass; no skipped CTest cases |
| App ASan/UBSan | 42/42 pass; no skipped CTest cases |
| Mandatory managed vcan | device-config, app, CANopen and SocketCAN cases execute and pass in both builds |
| Pinned Humble DDS command | Pass: context/shutdown race, status/probe, SIGTERM, node restart, paused simulation clock, server loss/reconnection |
| CI integration | `Humble IPC DDS` added for runtime/full scope and required by aggregate job; routing and actionlint pass; no new remote Actions run |
| Cross / ELF | Locked GCC11.4 image and actual target sysroot; app, fixture, config-check pass; 113 C++ source/header hashes match final source |
| Scoped static checks | C++ formatting, Python Ruff/LSP, Bash syntax and actionlint pass; clang-tidy exits 0 with six existing warnings |
| Direct board TCP | Two fixture instances, 226 healthy samples, receipt expiry and restart recovery pass |
| Network interruption | Three instances, 345 healthy samples; live same-server stream interrupted for 2.114882s and recovered 1.011365s after reconnect |
| Consumer expiry | 500ms host receipt deadline; four fresh→stale events in final network trial |
| Cleanup | Board fixture/node stopped, no stage sockets/listening test port; disposable host container removed |

The final scoped regression after sorting the migrated logger-health include passes
`linux_platform_integration` in both builds. The full 42-case logs precede that
include-only change; the final cross snapshot includes it. CI routing was rerun
after adding the remote observer path. `shellcheck` is unavailable in this host;
Bash syntax and the actual pinned-container command were checked. A final C++ LSP
probe confirmed four files clean; two still timed out at 10s. Those checks remain
inconclusive, with compile and clang-tidy evidence covering the changed C++.

## Owner work under IPC load

`owner_max_work_us` spans UART/source/control publication, batch/cycle logging and
IPC, starting after the scheduled CAN event wait. CAN waiting/dispatch before that
boundary and shutdown are excluded. `ipc_max_poll_us` measures the IPC call alone.
These are elapsed host measurements, not RK3588 hard-real-time acceptance.

| Scenario | Debug requests / owner max / IPC max | ASan requests / owner max / IPC max |
| --- | --- | --- |
| Non-executing probe | 209 / 348us / 105us | 211 / 777us / 156us |
| Backpressured peer | 532 / 306us / 171us | 531 / 890us / 217us |
| Flood, batches of 32 | 678 / 349us / 111us | 678 / 806us / 159us |

Each runs 401 measured owner cycles with zero 10ms owner-work overruns; peers are
dropped under backpressure, SBUS output continues and all IPC responses remain
non-executing. Raw timing records are in `TIMING.json` and compressed CTest logs.
The unmanaged SocketCAN test prints an informational SKIP for unavailable host
capabilities; its independent managed vcan test passes. That message is preserved
and is distinct from a skipped CTest case.

## Board-to-host link

The identified aarch64 board uses native `/opt/ros/humble`, `rmw_fastrtps_cpp`
6.2.10 and Fast DDS 2.6.12. The host uses the immutable image in
`docker/ros2/image.lock`, `ROS_LOCALHOST_ONLY=0`, domain 186. SSH source-address
observations show a routed/NAT boundary; the exact reason UDP DDS failed is not
established.

The board runs only the existing device-free fixture and bridge under its user
test directory. Fixture SHA256 is
`63ce6f61eb65575e1a55cc67820bd071258025daed2ce847c2db71316c9e6784`;
bridge SHA256 is `520f049b1c10d6de281856c1a67579ff9e94f46732044491474ab7aff613661d`.
The fixture is reused from prior verified staging. Its IPC server source/header,
UniqueFd header and fixture source match the prior compiled-input manifest; it
does not exercise the new app timing diagnostics on target. `BUILD.json` describes
the separately built current app/fixture/config-check artifacts, not a new physical
app deployment. `SOURCE_INPUTS.json` is the final 113-file C++ identity; full-tree
snapshot metadata records its own build-time scope, while `FILES.json` pins the
current verification code separately.

`host-tcp.xml` / `board-tcp.xml` are process-scoped test profiles. TCP transport
client/server roles are independent of DDS subscriber/publisher roles. The host
initiates an outbound connection to the board listener; no discovery server,
SSH tunnel, new package, system service, firewall or physical link change is used.
The final network trial uses a disposable Docker bridge container and temporarily
disconnects only that container's network attachment. The consumer continues
running during isolation. The strengthened oracle requires a valid sample within
200ms before removal, expiry during isolation, no healthy sample from 500ms after
removal until restoration, and fresh samples from the **same fixture instance**
after restoration. Sequence/capture/cycle values must advance in each session.

This proves receipt freshness. Source-state age is not verified and board/host
`CLOCK_MONOTONIC` values are never subtracted. No remote probe is sent. Targets,
source and execution/authority fields remain zero/false, and fixture feedback is
synthetic. Physical CAN/UART/drive, RViz robot data, production and motion remain
outside this acceptance.

## Preserved failed or incomplete attempts

- `default-*` and `udp-*`: zero samples / no publishers; discovery assertions fail.
- `tcp-proxy-*` and `tcp-proxy-wan-*`: TCP proxy configuration attempts produce no
  DDS data. They are failed test setups, not accepted interruption evidence.
- `tcp-container-initial/`: the original PASS output is preserved. Independent
  review finds interruption overlapped a fixture exit; verdict is
  `INCOMPLETE_NETWORK_CAUSALITY`. The final live same-instance trial supersedes its
  interruption claim and is archived separately.
- `cpp-initial/`: the earlier source snapshot/build metadata remains, before
  the include-order formatting correction.
- An initial build failed because the workspace already had deleted
  `easylogger_port_status.h`. The deletion remains; the existing C ABI declarations
  now reside in `stderr_sink_status.h`, with both includes updated. The pre-existing
  `.clangd` change is also preserved.
- `static.log`: existing signed-bitwise warnings in device parameters and the Linux
  platform test, plus unchecked-optional warnings in the existing `Result::value`
  contract. No source changes here disable checks or suppress those warnings.

## Reproduction and artifact use

From the repository root, configure/build the existing app Debug and sanitizer
presets and run their CTest suites. For real local DDS, pull the locked Humble
image as documented in `docker/ros2/README.md`, then run:

```sh
bash scripts/test/test_ros2_ipc.sh
python3 scripts/test/test_ci_scope.py
```

For board diagnostics, `scripts/test/test_ros2_ipc_remote_status.py` is the reusable
host observer. `run_fixture.py`, `run_remote.py`, process XML profiles and their
execution JSON record the bounded test setup. The runners use the checkout root,
its `out/verification/ipc-verification-20261009` output directory and the recorded
user stage; review/adapt endpoints before a future run. The archived authorization
is evidence of this software-only task, not a physical-operation permit.

`RESULT.json` contains scope, verdicts and limits. Logs/JUnit/status captures are
stored with deterministic gzip; `SHA256SUMS` pins all archived files except itself.
No current change was committed, pushed or deployed to production.
