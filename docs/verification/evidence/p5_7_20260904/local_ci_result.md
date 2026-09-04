# Phase 5 closure local CI result

Date: 2026-09-04
Source revision before documentation commit:
`0d32ca928e6c3969bd8a1007870a10e1b7e5cb27`

Result: PASS.

The local run reproduced every project step in `.github/workflows/ci.yml`:

- ShellCheck over project shell scripts: pass;
- checksum-pinned Hadolint v2.12.0 over `docker/cross/Dockerfile`: pass;
- Python syntax checks: pass;
- CANopen dependency regression: pass;
- P5.1 host qualification: pass (25 passed, one capability skip expected at
  this stage);
- Host Release non-SocketCAN suite: pass (25 passed, one managed-vcan
  capability skip expected at this stage);
- SocketCAN lifecycle and managed-vcan suite: 2/2 pass with required PF_CAN and
  network-namespace capabilities;
- Phase 1 negative-path script tests: pass;
- sysroot content-manifest regressions: pass.

The first SocketCAN invocation inside the restricted command sandbox failed
because PF_CAN socket creation was denied before the missing-interface assertion
could reach `if_nametoindex`; managed-vcan was skipped for the same capability
boundary. The unchanged CTest command passed 2/2 outside that restriction. This
is classified as an execution-environment capability failure, not a product or
test failure.
