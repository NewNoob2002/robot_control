# RK3588 CAN TX worker review — 2026-09-14

The running kernel contains a driver-owned retry mechanism capable of continuing
after the sending application exits. This is now supported by the target's
actual on-disk machine code, not only a generic queued-socket experiment.
Which individual worker invocation or controller retry completed the late
physical frame remains untraced. The failed cable trial remains failed.

Drive power stays off. This review used repository downloads, local
disassembly, file reads and RTM_GETLINK. No physical CAN frame, interface
configuration change, live-memory/register access, trace enable, kernel build,
installation or reboot occurred.

## Kernel and source identity

- Target kernel: 6.1.84; image package and headers package both 6.1.84-1.
- The on-disk image banner equals the running /proc/version. Its note section
  equals /sys/kernel/notes byte-for-byte. Image SHA256:
  f212c3c71bebc0d9c89e6951e5364d3a12bde8fe5deb06762ae0e968ec47a204.
- System.map resolves the CAN functions. Their bounded code slices were read
  from the disk image using the _text-relative offsets and disassembled locally.
  The image ARM64 header and banner validate this address mapping.
- Target configuration has LOCALVERSION_AUTO disabled, empty local version and
  build salt, and no .scmversion. The original full-tree commit/local patch set
  cannot be recovered from those fields.
- Official LubanCat/kernel branch lbc-develop-6.1 currently resolves to pinned
  commit 17ae1445226e5b3ebd76c15ce30b8f894ab79a84, whose Makefile is 6.1.99.
  This remains a source candidate, not a claim of exact full-tree provenance.
  Official history includes the 6.1.84 stable update, but that version label
  alone does not identify the target vendor merge/local modifications.
- The last driver-file change in this candidate history is 4b82c4e6018f,
  changing an RK3566/RK3568 CPU-version comparison. It does not change the TX
  worker. The machine-code conclusions below do not depend on assuming the
  newer full kernel matches the target.

## Confirmed target behavior

**Driver retry worker.** The target has rockchip_canfd_tx_err_delay_work at
ffff8000089ee948. Its disassembly reads mode/error status, then both paths
converge on a write of value 1 to register base + 4 (CAN_CMD/CAN_TX0_REQ in
the source candidate). It then converts delay_time_ms with
__msecs_to_jiffies and calls schedule_delayed_work.isra.0 at
ffff8000089ee5d0. That helper calls queue_delayed_work_on. There is no socket
owner, command age or CAN-state guard in this worker's code path.

The 500000 bit/s path assigns delay_time_ms=1; this is a requested jiffy-based
delay, not a measured 1 ms real-time period. Candidate source and extracted
target instructions agree on this behavior. Successful-TX interrupt processing
cancels the work item, completes the echo frame and increments TX packets.
Warning/passive handling is distinct from the bus-off stop/restart path.

**Socket close does not perform device close.** CANopenLinux closes its raw
socket during teardown. That is not the netdevice ndo_stop callback. A
previously submitted request can therefore remain in the driver/controller
transmit lifecycle after process exit. This is consistent with the physical
evidence: application exit at TX314, followed by a single speed upload and TX315.
It does not retrospectively prove every hardware retry or the exact enqueue time.

**One-shot is not advertised.** Read-only RTM_GETLINK returns extended controller
mode capability mask 0x17, with active flags 0. The target's own UAPI header
decodes 0x17 as loopback, listen-only, triple sampling and bus-error reporting.
ONE_SHOT is bit 0x08 and is absent. The ordinary ctrlmode mask field is zero;
capabilities come from the nested extended attribute, not that ordinary field.
No attempt was made to set one-shot or alter restart-ms.

**Device-close ordering needs review.** Actual target code calls queue stop,
controller stop, close_candev, pm_runtime_put, then cancel_delayed_work_sync.
The worker can rewrite mode/request registers and requeue itself, so this
ordering exposes a potential interval between device stop/power release and
worker quiescence. A race was not injected or reproduced on hardware; this is
a concrete static concurrency finding, not a claim that it caused this trial.

The error path also has its own stop/start sequence for bus-off. The physical
trial did not record bus-off. Changing restart-ms alone must not be treated as
disabling the separate TX work item or establishing a safe cancellation contract.

## Repair implications

Application-side SDO cancellation is necessary but cannot by itself satisfy
the strict post-error no-request gate on this transport. The repair must
coordinate TX invalidation, work cancellation, controller-request abort and
pending echo/buffer lifetime at the driver/platform boundary. Workers must not
resubmit after stop/invalidation, and hardware/clock teardown must follow the
appropriate quiescence ordering. Hardware retry/abort semantics still require
matching device documentation and validation; simply deleting the vendor retry
workaround is not a verified solution.

The explicit privileged interface-inhibit path is now implemented and passes
host, sanitizer, isolated-vcan and cross-build checks; see
[the userspace inhibitor record](P6_USERSPACE_CAN_INHIBITOR.md). It remains a
mitigation pending target and strict physical dual-capture validation. It cannot
be called a complete physical fix while the close/cancellation ordering and
controller abort behavior remain unqualified. No motion reauthorization may
result from automatic communication recovery. No production patch or
unverified kernel upgrade was introduced.

Evidence is in [driver review](evidence/p6_driver_review_20260914/): pinned
source manifests/history, target provenance and UAPI header, read-only netlink
attributes, matched image metadata, function slices, disassembly and callee
symbols. The earlier [delayed-TX report](P6_DELAYED_TX_INVESTIGATION.md) retains
physical correlation and isolated experiments. Exact full-tree build provenance
and physical validation of a repair remain open.
