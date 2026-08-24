# Phase 4 RK3588 Passive Target Evidence

Status: **PASS**

Evidence date: 2026-08-24

Source revision: `33b879c55b73097ee310506c606b31915c759dc9`

Branch: `codex/phase4-vcan-link-reopen`

## Conclusion

The authorized RK3588 identity and CAN inventory passed. The target is
`lubancat`, Ubuntu 22.04.5 LTS on aarch64, reached through the local SSH alias
`robot-dev` as the non-root user `cat` (UID/GID 1000). The operator then
explicitly selected `can0`, 500000 bit/s, and `restart-ms 100`, and
authorized one temporary SCP deployment.

Phase 4 target evidence is complete. A locked Ubuntu 22.04 arm64 sysroot
produced a warnings-as-errors `rk3588-debug` build of the receive-only probe.
The
aarch64 ELF was copied without overwrite to
`/tmp/robot-control-can-probe-33b879c5`; local and target SHA-256 values match
`09e191eb70561559cf9f00945424a8dff4cb8100bc243398cc6aee59f62b9854`,
and target `--help` and dynamic dependency resolution pass.

The operator then configured `can0` manually. A fresh SSH check confirmed
`UP` and `LOWER_UP`, CAN state `ERROR-ACTIVE`, bitrate 500000,
`restart-ms 100`, ifindex 2, and zero bus-error/RX/TX counters. As UID/GID
1000, the probe opened and bound the socket successfully. A 5000 ms run exited
0 with `reason=deadline frames=0`. A 60000 ms run received SIGTERM after
about one second, printed `reason=signal signal=15 frames=0`, and exited 143.
Postflight found no remaining probe process or CAN receiver and confirmed that
`can0` stayed UP/ERROR-ACTIVE with unchanged zero counters.

After the analyzer started periodic traffic, a final 3000 ms receive-only run
captured 31 frames and exited 0 with a deadline summary. Every observation
preserved `raw_can_id=0x000007ff`, `payload_length=8`, `len8_dlc=0`,
`data=48504d5200000001`, a raw kernel timestamp, and
`rx_queue_overflow=none`. The timestamp values were not converted or used for
deadline/safety decisions, and overflow `none` was not interpreted as a zero
drop count. Phase 4 RK3588 passive target evidence is complete. No CAN frame,
CANopen/CiA402 command, or motion operation was sent by this validation.

## Confirmed passive code path

CodeGraph re-read the current source and confirmed:

- `main()` -> `parse_options()` -> `run_probe()` ->
  `TerminationEvent::create()` -> `CanSocket::open()` -> repeated
  `CanSocket::receive()`.
- `run_probe()` enables raw `SO_TIMESTAMPNS` and `SO_RXQ_OVFL` receive
  metadata, prints it without conversion or delta calculation, and exits on a
  monotonic deadline or consumed SIGINT/SIGTERM.
- The probe has no call to `CanSocket::send()`, no link configuration or
  recovery path, and no CANopen, CiA402, arbitration, robot-safety, or motion
  dependency.

The target deadline and SIGTERM results below exercise this exact passive path.

## Target identity

| Field | Raw result | Result |
| --- | --- | --- |
| SSH BatchMode | SSH command exit 0 | PASS |
| Hostname | `lubancat` | PASS |
| Kernel | `Linux lubancat 6.1.84 #1 SMP Fri Jul  3 10:19:32 CST 2026 aarch64 aarch64 aarch64 GNU/Linux` | PASS |
| OS | `Ubuntu 22.04.5 LTS` (Jammy) | PASS |
| User | `uid=1000(cat) gid=1000(cat) groups=1000(cat),20(dialout),27(sudo),29(audio),44(video)` | PASS |
| UID / GID | `1000` / `1000` | PASS |
| Groups | `cat dialout sudo audio video` | PASS |
| Target time | `2026-08-24T09:57:25+08:00` | PASS |
| SSH target alias | `robot-dev`; network address omitted from versioned evidence | PASS |

Membership in the `sudo` group was first observed through `id`. One later,
explicitly authorized `sudo -n` command failed before privilege elevation;
no password was used.

## Read-only CAN inventory

`ip -details -statistics link show type can` returned exactly two CAN
interfaces:

| Field | `can0` | `can1` |
| --- | --- | --- |
| ifindex | `2` | `3` |
| ARPHRD / link type | sysfs `280`; `link/can` | sysfs `280`; `link/can` |
| Flags | `<NOARP,ECHO>`; sysfs `0x40080` | `<NOARP,ECHO>`; sysfs `0x40080` |
| `UP` / `LOWER_UP` | absent / absent | absent / absent |
| operstate | `down` | `down` |
| CAN state | `STOPPED` | `STOPPED` |
| bitrate | not reported while stopped/unconfigured | not reported while stopped/unconfigured |
| restart-ms | raw value `1` | raw value `1` |
| controller clock | raw value `150000000` | raw value `150000000` |
| parent device | `fea60000.can` | `fea70000.can` |
| sysfs driver | `/sys/bus/platform/drivers/rockchip_canfd` | `/sys/bus/platform/drivers/rockchip_canfd` |
| RX/TX counters | all reported values `0` | all reported values `0` |

Reading `/sys/class/net/{can0,can1}/carrier` returned `Invalid argument` and
made each combined sysfs command exit 1; the requested ifindex, type,
operstate, and flags were still returned before that failure. No state-changing
fallback was attempted.

The initial table records discovery state before the authorized external
configuration. Current SSH-verified `can0` state is:

| Field | Raw result |
| --- | --- |
| ifindex | `2` |
| Flags | `<NOARP,UP,LOWER_UP,ECHO>` |
| qdisc / operstate | `pfifo_fast` / `UP` |
| CAN state | `ERROR-ACTIVE` |
| bitrate / sample point | `500000` / `0.866` |
| restart-ms | raw value `100` |
| bus-error counters | tx `0`, rx `0` |
| restart/error counters | all reported values `0` |
| RX/TX counters | all reported values `0` |

CAN socket/runtime and passive traffic gates: **PASS**. The final capture
observed the expected ID and payload length with raw ancillary metadata.

## Target tool gate

| Check | Exit | Raw result | Result |
| --- | ---: | --- | --- |
| Cross build | 0 | GCC/G++ 11.4.0, Debug, warnings-as-errors; can probe linked | PASS |
| Local `file` | 0 | ELF64 PIE, ARM aarch64, interpreter `/lib/ld-linux-aarch64.so.1` | PASS |
| Local SHA-256 | 0 | `09e191eb70561559cf9f00945424a8dff4cb8100bc243398cc6aee59f62b9854` | PASS |
| SCP destination existence precheck | 2 | target path did not exist | PASS |
| SCP to target | 0 | one file copied to the authorized temporary path | PASS |
| Target owner/mode/size/mtime | 0 | `cat:cat`, `0755`, 781240 bytes, `2026-08-24 11:33:42 +0800` | PASS |
| Target SHA-256 | 0 | matches local SHA-256 | PASS |
| Target `file` | 127 | `file` command is not installed | PASS with recorded environment limitation |
| Target `readelf -h` | 0 | ELF64 PIE, AArch64 | PASS |
| Target `ldd` | 0 | loader, libc, libstdc++, libgcc_s, and libm resolved | PASS |
| Target `--help` | 0 | expected receive-only CLI usage and duration range | PASS |

Target tool gate: **PASS**. The target lacks `file(1)`; that environment
limitation is recorded rather than hidden. Local `file`, identical target
hash, target `readelf -h`, target dynamic-loader resolution, and target
execution of `--help` establish the required aarch64 runtime compatibility.
The temporary file remains present because cleanup authorization was left
unresolved and is therefore treated as denied.

## Runtime and frame evidence

| Evidence | Result | Reason |
| --- | --- | --- |
| Non-root identity | PASS | SSH session remained UID/GID 1000 |
| Non-root SocketCAN open/bind | PASS | start record reports `interface="can0" interface_index=2` |
| Bounded deadline run | PASS | exit 0; `reason=deadline frames=0` |
| SIGTERM summary and exit 143 | PASS | signal 15; `reason=signal frames=0`; wait/SSH exit 143 |
| Passive frame fields | PASS | 31 observations: raw ID `0x000007ff`, payload length 8, len8 DLC 0, data `48504d5200000001` |
| Kernel timestamp fields | PASS | raw sec/nsec present on every captured observation; no conversion performed |
| RX queue overflow field | PASS | raw field preserved as `none`; not interpreted as zero or a delta |

No permission error or errno was produced by SocketCAN. The earlier zero-frame
deadline and SIGTERM runs occurred before analyzer traffic was enabled; the
later bounded capture supplied the required frame and metadata evidence.

## Acceptance matrix

| Acceptance item | Evidence | Result | Notes |
| --- | --- | --- | --- |
| Target identity is explicit | BatchMode SSH identity output | PASS | Current RK3588/Linux identity recorded |
| CAN candidates are inventoried | `ip show type can`, per-device `ip`, and sysfs | PASS | `can0` and `can1` recorded without guessing |
| One externally configured UP interface is explicit | Fresh SSH `ip` output for `can0` | PASS | UP/LOWER_UP, ERROR-ACTIVE, 500000 bit/s, restart-ms 100 |
| Agent performed no interface configuration operation | Command audit below | PASS | Operator configured the link manually; agent only verified it read-only |
| Target probe is available | Cross build, SCP, hash, `ldd`, and `--help` | PASS | Temporary target file retained at the authorized path |
| Unprivileged data access | start/deadline/signal output as UID/GID 1000 | PASS | No sudo used for probe execution |
| Passive tool has no send path | CodeGraph current-source path | PASS | Probe reaches `open()` and `receive()` only |
| Bounded run and deadline exit | 5000 ms target run with nested timeout bounds | PASS | exit 0 and exact deadline summary observed |
| SIGTERM cancellation | 60000 ms run, one-second signal harness | PASS | signal 15 summary; kill exit 0; wait/SSH exit 143 |
| Raw ID/DLC/payload preserved | 3000 ms target capture, 31 frames | PASS | Expected raw ID and payload length; exact data and len8 DLC retained |
| Timestamp/overflow preserved raw | All 31 target observations | PASS | Timestamp raw; overflow raw `none`; no conversion, delta, or safety inference |
| No motion or drive write operation | Command and call-path audit | PASS | No transmit, CANopen, CiA402, arbitration, safety, or motion call ran |

## Command record

### Local baseline and CodeGraph

| Command | Exit | Exact result |
| --- | ---: | --- |
| `rtk git status --short --branch` | 0 | branch `codex/phase4-vcan-link-reopen`; existing modifications: `docker/cross/image.lock`, `docs/plans/PHASE4_SOCKETCAN_FOUNDATION.md`, and untracked `docs/verification/PHASE4_TARGET_EVIDENCE.md` |
| `rtk git rev-parse HEAD` | 0 | `33b879c55b73097ee310506c606b31915c759dc9` |
| `rtk git branch --show-current` | 0 | `codex/phase4-vcan-link-reopen` |
| `rtk test -d .codegraph` | 2 | RTK forwarded `-d` as a shell option: `sh: 0: Illegal option -d`; CodeGraph availability was then proven by successful `codegraph explore` calls |
| `rtk codegraph explore "Trace robot-control-can-probe from main through CanSocket::open() and CanSocket::receive(); show the relevant current source and confirm whether any send, interface configuration, CANopen, CiA402, arbitration, safety, or motion path is reachable."` | 0 | Current probe/socket source returned; open/receive-only path confirmed |
| `rtk codegraph explore "Show the complete current tools/can_probe/main.cpp source for run_probe, print_observation, print_error, and main, and the direct call path from main to CanSocket::open and CanSocket::receive."` | 0 | Current `run_probe()`, frame output, and socket receive source returned |

No source file changed, so format and static-analysis jobs were not rerun.
An arm64 Debug cross build was run solely to produce the authorized target
probe. Existing host/vcan evidence remains separate from this target attempt.

### Target commands

Every target command used `ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev`.
The exact commands and process exits were:

| Command | Exit |
| --- | ---: |
| identity audit: `hostname`; `uname -a`; `sed -n "1,200p" /etc/os-release`; `id`; `id -u`; `id -g`; `id -Gn`; `date --iso-8601=seconds`; attempted shell extraction of `SSH_CONNECTION` | 0; each identity subcommand reported 0; the attempted target-address extraction was empty and was superseded by the next command |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'printenv SSH_CONNECTION'` | 0 |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'ip -details -statistics link show type can'` | 0 |
| initial generic sysfs loop over `/sys/class/net/*` | 0, but unusable: quote/expansion handling produced `sed` command-not-found errors and a false count of zero; superseded by explicit reads of the two interfaces returned by `ip` |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'ip -details -statistics link show dev can0'` | 0 |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'ip -details -statistics link show dev can1'` | 0 |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'printf "ifindex="; cat /sys/class/net/can0/ifindex; printf "type="; cat /sys/class/net/can0/type; printf "operstate="; cat /sys/class/net/can0/operstate; printf "flags="; cat /sys/class/net/can0/flags; printf "carrier="; cat /sys/class/net/can0/carrier'` | 1 (`carrier`: `Invalid argument`) |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'printf "ifindex="; cat /sys/class/net/can1/ifindex; printf "type="; cat /sys/class/net/can1/type; printf "operstate="; cat /sys/class/net/can1/operstate; printf "flags="; cat /sys/class/net/can1/flags; printf "carrier="; cat /sys/class/net/can1/carrier'` | 1 (`carrier`: `Invalid argument`) |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'readlink -f /sys/class/net/can0/device/driver'` | 0 |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'readlink -f /sys/class/net/can1/device/driver'` | 0 |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'command -v robot-control-can-probe'` | 1 |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'find /opt/robot-control -type f -name robot-control-can-probe -print'` | 1 (`/opt/robot-control` absent) |

The combined identity command's full remote script was:

```sh
rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'set +e; printf "BEGIN hostname\n"; hostname; rc=$?; printf "EXIT hostname=%s\n" "$rc"; printf "BEGIN uname\n"; uname -a; rc=$?; printf "EXIT uname=%s\n" "$rc"; printf "BEGIN os-release\n"; sed -n "1,200p" /etc/os-release; rc=$?; printf "EXIT os-release=%s\n" "$rc"; printf "BEGIN id\n"; id; rc=$?; printf "EXIT id=%s\n" "$rc"; printf "BEGIN uid\n"; id -u; rc=$?; printf "EXIT uid=%s\n" "$rc"; printf "BEGIN gid\n"; id -g; rc=$?; printf "EXIT gid=%s\n" "$rc"; printf "BEGIN groups\n"; id -Gn; rc=$?; printf "EXIT groups=%s\n" "$rc"; printf "BEGIN time\n"; date --iso-8601=seconds; rc=$?; printf "EXIT time=%s\n" "$rc"; set -- $SSH_CONNECTION; printf "ssh_target_address=%s\n" "$3"; printf "EXIT ssh_target_address=0\n"'
```

The discarded generic sysfs command was:

```sh
rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'set +e; count=0; for path in /sys/class/net/*; do [ -r "$path/type" ] || continue; type=$(sed -n "1p" "$path/type"); [ "$type" = 280 ] || continue; count=$((count + 1)); name=${path##*/}; printf "BEGIN_CAN_INTERFACE name=%s\n" "$name"; for field in ifindex type operstate flags carrier; do printf "%s=" "$field"; sed -n "1p" "$path/$field" 2>/dev/null; rc=$?; printf "EXIT_%s=%s\n" "$field" "$rc"; done; printf "driver_path="; readlink -f "$path/device/driver"; rc=$?; printf "EXIT_driver_path=%s\n" "$rc"; printf "END_CAN_INTERFACE name=%s\n" "$name"; done; printf "can_interface_count=%s\n" "$count"'
```

### Authorized target-readiness re-entry

| Command | Exit | Exact result |
| --- | ---: | --- |
| `rtk codegraph explore "Locate the existing RK3588 Ubuntu 22.04 aarch64 cross-build path, CMake preset/toolchain, build script, and expected robot-control-can-probe artifact path. Show relevant current source or configuration."` | 0 | Located the locked sysroot and RK3588 presets |
| `rtk env ROBOT_CONTROL_SYSROOT=/home/gtc/Desktop/workspace/Linux_PROJ/robot_control/sysroots/rk3588-ubuntu2204 ROBOT_CONTROL_PRESET=rk3588-debug ./scripts/build/build_rk3588.sh` (restricted sandbox) | 2 | sysroot validation passed; Docker socket access was denied by the sandbox |
| same cross-build command outside the restricted sandbox | 0 | 23 Ninja steps passed; arm64 can probe linked with warnings-as-errors |
| `rtk file out/build/cross/rk3588-debug/tools/can_probe/robot-control-can-probe` | 0 | ELF64 PIE, ARM aarch64, dynamic loader `/lib/ld-linux-aarch64.so.1` |
| `rtk stat -c '%U %G %a %s %y %n' out/build/cross/rk3588-debug/tools/can_probe/robot-control-can-probe` | 0 | `gtc gtc 755 781240 2026-08-24 11:33:42.022366399 +0800` |
| `rtk sha256sum out/build/cross/rk3588-debug/tools/can_probe/robot-control-can-probe` | 0 | `09e191eb70561559cf9f00945424a8dff4cb8100bc243398cc6aee59f62b9854` |
| `rtk readelf -h .../robot-control-can-probe` | 0 | ELF64, little-endian, AArch64, PIE |
| `rtk readelf -l .../robot-control-can-probe` | 0 | expected aarch64 interpreter; non-executable stack |
| `rtk readelf -d .../robot-control-can-probe` | 0 | libc/libstdc++/libgcc_s/loader dependencies; no RPATH/RUNPATH |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'hostname; id; date --iso-8601=seconds'` | 0 | `lubancat`, UID/GID 1000, `2026-08-24T11:35:12+08:00` |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'ip -details -statistics link show dev can0'` | 0 | `can0` remained DOWN/STOPPED, restart-ms raw value 1 |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'ss -a -f can'` | 255 | target iproute2 does not support the `can` ss family |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'cat /proc/net/can/rcvlist_all'` | 0 | no receiver entry for any, can0, or can1 |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'ls -ld /tmp/robot-control-can-probe-33b879c5'` | 2 | destination absent before copy |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'findmnt -T /tmp -no TARGET,FSTYPE,OPTIONS'` | 0 | root ext4 mount, read-write, no `noexec` flag |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'df -Pk /tmp'` | 0 | 21750632 KiB available |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'command -v ip; ip -Version; grep ^CapEff: /proc/self/status'` | 0 | `/usr/sbin/ip`, iproute2 5.15.0, `CapEff: 0000000000000000` |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'getcap /usr/sbin/ip'` | 0 | no file capability output |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'stat -c %U:%G:%a:%A:%n /usr/sbin/ip'` | 0 | root-owned symlink metadata recorded |
| `rtk scp -p -o BatchMode=yes -o ConnectTimeout=10 out/build/cross/rk3588-debug/tools/can_probe/robot-control-can-probe robot-dev:/tmp/robot-control-can-probe-33b879c5` | 0 | authorized single-file copy completed |
| target `stat` of the temporary probe | 0 | `cat:cat`, mode 0755, 781240 bytes |
| target `file` of the temporary probe | 127 | target lacks the `file` command |
| target `sha256sum` of the temporary probe | 0 | exact local hash match |
| target probe `--help` | 0 | expected receive-only CLI output |
| target `ldd` of the temporary probe | 0 | all required target libraries resolved |
| final preflight identity/hash/receiver checks | 0 | target, artifact hash, DOWN state, and empty CAN receive list reconfirmed |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'sudo -n ip link set dev can0 type can bitrate 500000 restart-ms 100'` | 1 | `sudo: a password is required`; `ip` did not run |
| authorized `sudo -n ip link set dev can0 up` | not run | stopped after the first command failed |
| post-failure `ip -details -statistics link show dev can0` | 0 | unchanged DOWN/STOPPED state; raw `restart-ms 1` |

### Passive target runtime

The operator manually ran the two authorized `sudo ip link` commands. Their
shell exit codes were not printed in the supplied transcript, so this evidence
does not invent them; the resulting configuration is independently verified by
the following exit-0 SSH `ip` output.

| Command | Exit | Exact result |
| --- | ---: | --- |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'hostname; id; date --iso-8601=seconds'` | 0 | `lubancat`, UID/GID 1000, `2026-08-24T11:54:26+08:00` |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'ip -details -statistics link show dev can0'` | 0 | UP/LOWER_UP, ERROR-ACTIVE, bitrate 500000, restart-ms 100, zero counters |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'sha256sum /tmp/robot-control-can-probe-33b879c5'` | 0 | exact reviewed artifact hash match |
| `rtk ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'cat /proc/net/can/rcvlist_all'` | 0 | no receiver before the test |
| `rtk timeout --signal=TERM --kill-after=2s 20s ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'timeout --signal=TERM --kill-after=2s 12s /tmp/robot-control-can-probe-33b879c5 --interface can0 --duration-ms 5000'` | 0 | `event=start ... interface="can0" interface_index=2 duration_ms=5000`; `event=summary reason=deadline frames=0` |
| `rtk timeout --signal=TERM --kill-after=2s 20s ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev '/tmp/robot-control-can-probe-33b879c5 --interface can0 --duration-ms 60000 & probe_pid=$!; sleep 1; kill -TERM "$probe_pid"; kill_rc=$?; wait "$probe_pid"; probe_rc=$?; printf "event=harness probe_pid=%s kill_exit=%s wait_exit=%s\n" "$probe_pid" "$kill_rc" "$probe_rc"; exit "$probe_rc"'` | 143 | signal summary reports signal 15 and frames 0; harness PID 8437, kill exit 0, wait exit 143 |
| postflight `ip -details -statistics link show dev can0` | 0 | still UP/ERROR-ACTIVE at 500000 bit/s, restart-ms 100, zero counters |
| postflight `cat /proc/net/can/rcvlist_all` | 0 | no remaining receiver |
| postflight `pgrep -af ^/tmp/robot-control-can-probe-33b879c5` | 1 | no remaining probe process |
| postflight `date --iso-8601=seconds` | 0 | `2026-08-24T11:57:22+08:00` |
| postflight target probe `stat` | 0 | temporary file remains `cat:cat`, mode 0755, 781240 bytes |

### Periodic frame capture

| Command | Exit | Result |
| --- | ---: | --- |
| preflight identity at `2026-08-24T16:36:30+08:00` | 0 | `lubancat`, UID/GID 1000 |
| preflight `ip -details -statistics link show dev can0` | 0 | UP/ERROR-ACTIVE, bitrate 500000, restart-ms 100; raw RX 10600 bytes/1325 packets, TX zero |
| preflight target probe SHA-256 | 0 | exact reviewed hash match |
| preflight `cat /proc/net/can/rcvlist_all` | 0 | no receiver before capture |
| `rtk timeout --signal=TERM --kill-after=2s 18s ssh -o BatchMode=yes -o ConnectTimeout=10 robot-dev 'timeout --signal=TERM --kill-after=2s 10s /tmp/robot-control-can-probe-33b879c5 --interface can0 --duration-ms 3000'` | 0 | 31 expected frames and deadline summary |
| postflight `ip -details -statistics link show dev can0` | 0 | still UP/ERROR-ACTIVE; raw RX 17176 bytes/2147 packets, TX zero |
| postflight `cat /proc/net/can/rcvlist_all` | 0 | no remaining receiver |
| postflight `pgrep -af ^/tmp/robot-control-can-probe-33b879c5` | 1 | no remaining probe process |
| postflight time | 0 | `2026-08-24T16:38:01+08:00` |
| target `command -v readelf; command -v objdump; command -v busybox` | 1 | `readelf` and `objdump` present; `busybox` absent |
| target `readelf -h /tmp/robot-control-can-probe-33b879c5` | 0 | ELF64, little-endian, PIE, AArch64 |

The complete capture stdout was:

```text
event=start probe_version=1 interface="can0" interface_index=2 duration_ms=3000
event=frame sequence=1 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560630 kernel_timestamp_nsec=122332575 rx_queue_overflow=none
event=frame sequence=2 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560630 kernel_timestamp_nsec=222330354 rx_queue_overflow=none
event=frame sequence=3 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560630 kernel_timestamp_nsec=322326382 rx_queue_overflow=none
event=frame sequence=4 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560630 kernel_timestamp_nsec=422327369 rx_queue_overflow=none
event=frame sequence=5 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560630 kernel_timestamp_nsec=522327480 rx_queue_overflow=none
event=frame sequence=6 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560630 kernel_timestamp_nsec=622326717 rx_queue_overflow=none
event=frame sequence=7 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560630 kernel_timestamp_nsec=722326537 rx_queue_overflow=none
event=frame sequence=8 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560630 kernel_timestamp_nsec=822327815 rx_queue_overflow=none
event=frame sequence=9 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560630 kernel_timestamp_nsec=922327927 rx_queue_overflow=none
event=frame sequence=10 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560631 kernel_timestamp_nsec=22328039 rx_queue_overflow=none
event=frame sequence=11 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560631 kernel_timestamp_nsec=122326984 rx_queue_overflow=none
event=frame sequence=12 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560631 kernel_timestamp_nsec=222327970 rx_queue_overflow=none
event=frame sequence=13 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560631 kernel_timestamp_nsec=322327207 rx_queue_overflow=none
event=frame sequence=14 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560631 kernel_timestamp_nsec=422327902 rx_queue_overflow=none
event=frame sequence=15 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560631 kernel_timestamp_nsec=522326264 rx_queue_overflow=none
event=frame sequence=16 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560631 kernel_timestamp_nsec=622326959 rx_queue_overflow=none
event=frame sequence=17 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560631 kernel_timestamp_nsec=722324737 rx_queue_overflow=none
event=frame sequence=18 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560631 kernel_timestamp_nsec=822326890 rx_queue_overflow=none
event=frame sequence=19 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560631 kernel_timestamp_nsec=922326127 rx_queue_overflow=none
event=frame sequence=20 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560632 kernel_timestamp_nsec=22325947 rx_queue_overflow=none
event=frame sequence=21 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560632 kernel_timestamp_nsec=122325183 rx_queue_overflow=none
event=frame sequence=22 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560632 kernel_timestamp_nsec=222325878 rx_queue_overflow=none
event=frame sequence=23 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560632 kernel_timestamp_nsec=322324531 rx_queue_overflow=none
event=frame sequence=24 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560632 kernel_timestamp_nsec=422323768 rx_queue_overflow=none
event=frame sequence=25 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560632 kernel_timestamp_nsec=522325046 rx_queue_overflow=none
event=frame sequence=26 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560632 kernel_timestamp_nsec=622323699 rx_queue_overflow=none
event=frame sequence=27 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560632 kernel_timestamp_nsec=722324394 rx_queue_overflow=none
event=frame sequence=28 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560632 kernel_timestamp_nsec=822323923 rx_queue_overflow=none
event=frame sequence=29 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560632 kernel_timestamp_nsec=922325201 rx_queue_overflow=none
event=frame sequence=30 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560633 kernel_timestamp_nsec=22323562 rx_queue_overflow=none
event=frame sequence=31 raw_can_id=0x000007ff payload_length=8 len8_dlc=0 data=48504d5200000001 kernel_timestamp_sec=1787560633 kernel_timestamp_nsec=122325716 rx_queue_overflow=none
event=summary reason=deadline frames=31
```

All timestamp and overflow fields above are retained verbatim. No timestamp
delta, clock-domain conversion, rate calculation, queue-drop calculation, or
safety inference was performed.

## Prohibited-operation audit

The re-entry added one explicitly authorized state change: SCP created exactly
`/tmp/robot-control-can-probe-33b879c5` after an absence check. No other
target file was written. One exact authorized `sudo -n` command was attempted
and failed before `ip` executed. No password was passed through a command,
stdin, file, or log. The operator later configured `can0` manually outside
the agent command set. The agent then performed only read-only interface checks,
receive-only probe runs, and one SIGTERM directed to the recorded probe PID.
The complete agent command set contained no:

- successful sudo, root shell, or privilege escalation;
- successful `ip link set`, interface up/down, bitrate, restart, or link
  recovery command;
- CAN transmit, `cansend`, `candump` injection, CANopen, or CiA402 command;
- `rsync`, package installation, service deployment, or overwrite of an
  existing target file;
- `modprobe`, service restart/modification, driver modification, or device-tree
  change;
- motor, drive-parameter, arbitration, safety-authority, or motion-control call;
- signal delivery other than SIGTERM to probe PID 8437.

The authorized temporary probe remains on the target because cleanup was left
as `allow/disallow` and was conservatively treated as disallowed.

## Final local audit

| Command | Exit | Result |
| --- | ---: | --- |
| `rtk git diff --check` | 0 | no whitespace errors reported |
| `rtk git status --short` | 0 | user-owned `docker/cross/image.lock` remains modified; README and the Phase 4 plan are modified; this evidence file remains untracked |
| `rtk git branch --show-current` | 0 | `codex/phase4-vcan-link-reopen` |
| `rtk git rev-parse HEAD` | 0 | `33b879c55b73097ee310506c606b31915c759dc9` |

No commit was created.

## Completion and residual target state

All Phase 4 RK3588 passive target acceptance items pass. The target remains in
the operator-configured state: `can0` is UP at 500000 bit/s with
`restart-ms 100`, the analyzer continues to provide external traffic, and
the temporary probe remains at `/tmp/robot-control-can-probe-33b879c5`.
Neither interface rollback nor temporary-file cleanup was authorized, so
neither was performed.
