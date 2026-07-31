# Local sysroots

This directory is ignored by Git except for this README.

Place generated target sysroots below it, for example:

```text
sysroots/rk3588-ubuntu2204/
```

Sysroot contents may include target-specific binaries and package metadata.
They are build inputs, not source, and must be stored/versioned externally by
manifest and checksum. Never commit a sysroot.
