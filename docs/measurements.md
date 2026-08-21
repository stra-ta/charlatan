# Measurements

All figures below are from the ARM64 Ubuntu 26.04 Lima VM on Apple Virtualization.

They are virtualized development measurements, not bare-metal device performance.

## Syscall path result

`charlatan-bench 10000` completed 10,000 write/read event round trips in 0.0120494 seconds, or 829,914 events per second.

The measurement alternates one `write()` and one `read()` so the 128-event queue never overflows.

It measures syscall and driver overhead rather than peak batching throughput.

The run used an Apple M1 host with Lima Apple Virtualization, a 4-vCPU and 3 GiB Ubuntu 26.04 ARM64 VM, Linux 7.0.0-28-generic, GCC 15.2.0, CMake 4.2.3, and a Debug CMake build.

`scripts/benchmark.sh` writes repeatable machine-readable JSON to the ignored `results/stream.json` path.

## Stress evidence

One two-second run completed 1,386,098 writes, 1,381,337 reads, and 381 resets with zero unexpected errno values.

A later two-second lifecycle run completed 521,407 writes, 516,096 reads, and 291 resets with zero unexpected errno values.

Ten consecutive integration runs passed after the deterministic producer-control race regression was added.

A later two-second active-worker run completed 914,326 writes, 908,379 reads, and 335 resets with zero unexpected errno values.

VM scheduling, shared-host load, and the intentionally overflowing queue make these values variable.
