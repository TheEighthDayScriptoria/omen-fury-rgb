# Contributing

Keep kernel changes transport-only. New user-visible modes belong in userspace
and require a reproducible trace or decompilation note establishing register
ordering, values, and timing. Never add generic WMI calls, unrestricted
registers, direct I801 access, or driver unbind behavior.

Run `make test` and `make module` before submitting changes. Hardware reports
should include the OMEN product, BIOS version, kernel version, command, target
DIMM, physical result, CLI diagnostics, and relevant kernel log lines. Do not
include serial numbers or other machine identifiers.
