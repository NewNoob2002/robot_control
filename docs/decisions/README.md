# Architecture Decision Records

ADRs are immutable decision history. Supersede an accepted ADR with a new ADR;
do not rewrite its historical decision.

| ADR | Decision | Status |
|---|---|---|
| [0001](0001-layer-and-dependency-boundaries.md) | Linux-native layered architecture | Accepted |
| [0002](0002-canopen-stack-and-logging-dependencies.md) | CANopenNode + CANopenLinux; no EasyLogger initially | Accepted |
| [0003](0003-debian11-target-sysroot.md) | Board-derived, versioned Debian 11 sysroot | Accepted |
| [0004](0004-single-owner-execution-model.md) | Single control owner and snapshot concurrency | Accepted |
| [0005](0005-supported-os-lifecycle.md) | Bullseye compatibility baseline with migration gate | Accepted |
| [0006](0006-rk3588-ubuntu2204-baseline.md) | Ubuntu 22.04 replaces Debian 11 target baseline | Accepted; supersedes ADR-0003 and ADR-0005 target choice |
