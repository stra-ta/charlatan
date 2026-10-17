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

A later two-second active-worker run completed 914,326 writes, 908,379 reads, and 335 resets with zero unexpected errno values.

VM scheduling, shared-host load, and the intentionally overflowing queue make these values variable.

## Rerun after the 0.3 rescope (ABI version 5)

Rerun 2026-10-17 on Apple M1 host, Lima Apple Virtualization, Debian 12 ARM64
guest, Linux 6.1.0-50-cloud-arm64, GCC 12.2.0, CMake 3.25.1, Debug build,
4 vCPU / 3 GiB.

| Workload | Result |
| --- | ---: |
| `charlatan-bench 10000 syscall` | 990,855 events/s (0.0100923 s) |
| `charlatan-bench 10000 mmap` | 350,357 events/s (0.0285423 s) |
| Two-second stress run | 778,102 writes, 774,987 reads, 377 resets, 0 unexpected |

The full integration suite passed, including three repeated mmap open/map/unmap
cycles before the suite ran, and a writable mapping still returns `EPERM`.

This run followed a real defect found by the VM verification: the module's
VM operations took a module reference only in `vm_ops->open`, which the kernel
does not call for the initial `mmap`. Every unmap therefore unbalanced the
module refcount by one `module_put`; after the integration suite's mmap test
the refcount underflowed, `try_module_get` began failing, and every later
`open()` returned `ENXIO` until reload. The fix takes the reference in
`charlatan_mmap` for the initial mapping and keeps the open callback for
fork/split duplicates, with one release per VMA teardown. The older numbers
above are from the pre-rescope tree and stay labeled to their environments.
