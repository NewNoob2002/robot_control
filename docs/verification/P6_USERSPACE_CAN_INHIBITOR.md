# P6 userspace CAN interface inhibitor — 2026-09-14

## Disposition

**SOFTWARE, TARGET AND AUTHORIZED PHYSICAL PASS.** This change adds a
userspace mitigation for the delayed RK3588 CAN transmission seen after the
failed cable-loss trial. It does not change the kernel or require a vendor SDK.
It passes host, sanitizer, isolated-vcan, RK3588 cross-build/ELF, target isolated
vcan and one raised-wheel physical cable-loss trial.

This remains a mitigation around the existing `rockchip_canfd` close path; it
does not change or claim to repair the kernel race described in
[the driver review](P6_ROCKCHIP_TX_WORKER_REVIEW.md).

## Implemented behavior

The Debug-only/default-OFF qualification build now produces
`robot-control-can-interface-inhibitor`. The helper accepts only `can0`, verifies
effective `CAP_NET_ADMIN`, and opens a receive-only CAN RAW error socket with all
normal frame filters removed. A private inherited `SOCK_SEQPACKET` channel ties
its lifetime to the qualification process.

The helper administratively sets `can0` down and verifies that `IFF_UP` is clear
when any of these events occurs:

- a CAN error frame is received;
- the qualification process explicitly requests inhibition;
- the parent channel closes because the parent exits or is terminated;
- the parent sends a malformed command.

The helper checks CAN errors before parent commands, so a simultaneous release
and error remains fail-closed. A successful, fully verified qualification
cleanup may send the explicit release command. The helper never brings `can0`
up, never transmits a CAN frame, and cannot operate on another interface.

`robot-control-zlac-qualification --external-loss-once` now requires both the
exact interface `can0` and an absolute `--interface-inhibitor` path. The helper
is armed before CAN lifecycle creation. Startup, lifecycle, readiness or
qualification failure explicitly inhibits the interface before process return.
If the helper already reacted to a CAN error, the parent consumes that verified
down acknowledgement. Reconnection cannot restore the interface or motion
authority; later recovery requires privileged interface setup, the existing
zero-target preflight and a new operator authorization.

## Privilege boundary

The main qualification process remained the unprivileged `cat` user. The target
helper and qualification executable were staged root-owned with group `cat` and
mode 0750. Only the helper received `cap_net_admin=ep`; the qualification
executable had no file capability. The exact staged helper path was passed to
`--interface-inhibitor`.

## Verification

The managed namespace test creates only `lo` and a virtual interface named
`can0`. It proves:

1. explicit release leaves `can0` up;
2. explicit inhibition sets `can0` down;
3. parent-channel EOF sets `can0` down;
4. an independently received CAN controller error sets `can0` down;
5. an interface name other than `can0` is rejected;
6. the qualification CLI rejects missing, relative, duplicate or inapplicable
   helper arguments without creating a CAN lifecycle;
7. a qualification startup/communication failure explicitly leaves namespace
   `can0` down.

Final software results:

- qualification Debug host tests: 68/68;
- qualification ASan/UBSan tests: 68/68;
- default-OFF Debug and Release tests: 28/28 each;
- LLVM analyzer/bugprone/performance/portability checks on changed C++: clean;
- `clang-format`, `shellcheck`, and `git diff --check`: clean;
- pinned offline RK3588 GCC 11.4 qualification and default Release builds: pass;
- qualification executable, inhibitor helper and default platform probe ELF
  audits: interpreter/dependencies/symbol versions valid, no RPATH/RUNPATH.

Evidence is retained in
[`evidence/p6_userspace_can_inhibit_20260914/`](evidence/p6_userspace_can_inhibit_20260914/).
The initial test-first compile failure is retained as `red_compile.log`.

Target and physical evidence is retained in
[the target attempt](evidence/p6_userspace_can_inhibit_target_20260914/)
and [the accepted retest](evidence/p6_userspace_can_inhibit_retest_20260914/).
The first target attempt is invalid because the cable was not removed. The new
operator-synchronized runner then passed:

- one real controller error was captured at 1789377758.304241;
- `can0` was observed DOWN 12.563 ms later;
- no RK3588 request followed the error in either capture;
- all 171 normal target frames match silent JCAN in order;
- the retained JCAN post-error window conservatively exceeds 24 s and therefore
  covers the historical 4.360145 s delayed request;
- right-wheel motion and normal stop, stationary left wheel, no reconnect
  restart or abnormal sound, powered-off final state and site safety were
  operator-confirmed.

The outer coordinator reached its 100000-frame bound after the acceptance
window because the drive repeatedly transmitted TPDO1 without an ACKing peer.
The target wrapper had already passed, and the bounded capture contains zero
post-error RK3588 requests. No retry was performed.

## Operational boundary

The helper leaves `can0` DOWN. Reconnection cannot restore communication or
motion authority. Any later interface-up action still requires drive power OFF,
the existing zero-target preflight and a new operator authorization. The result
qualifies this exact raised-wheel cable-loss mitigation; it does not qualify
power loss, loaded operation, other interfaces or a kernel fix.
