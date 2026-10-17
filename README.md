# Charlatan

[![CI](https://github.com/stra-ta/charlatan/actions/workflows/userspace.yml/badge.svg)](https://github.com/stra-ta/charlatan/actions/workflows/userspace.yml)

A Linux streaming character device and C++20 userspace harness for studying the
kernel boundary: a bounded FIFO behind `read()`, `write()`, `poll()`, `epoll()`,
and a versioned ioctl ABI, plus a read-only mmap page that publishes a
sequence-checked observation snapshot without handing queue ownership to
userspace.

## Measured snapshot

| Workload | Result |
| --- | ---: |
| 10,000 alternating write/read event round trips (syscall transport) | 9.91e5 events/s |
| 10,000 round trips with one snapshot read per iteration (mmap transport) | 3.50e5 events/s |

Rerun 2026-10-17: Apple M1 host, Lima Apple Virtualization, Debian 12 ARM64
guest, Linux 6.1.0-50-cloud-arm64, GCC 12.2.0, Debug build, ABI version 5,
4 vCPU / 3 GiB.
This measures syscall and driver overhead in one VM, not bare-metal peak
throughput.
The earlier Ubuntu 26.04 run stays in [docs/measurements.md](docs/measurements.md)
with its own environment label.

## What the harness checks

- Queue boundaries, wraparound, overflow accounting, reset, and blocked readers
- Poll and epoll readiness, including the reset priority notification
- Mmap snapshot stability, FIFO observation, and writable-mapping rejection
- Concurrent readers, writers, resets, and rapid open/close stress
- Kernel and userspace ABI agreement

Kernel integration and the checked-in benchmark run inside an ARM64 Linux VM,
because `/dev/charlatan0` is a kernel-boundary artifact.
Host machines without the module build and test the userspace harness only.

## Build

```sh
sudo ./scripts/install-deps.sh   # on the Linux VM or host
./scripts/build.sh               # userspace harness + kernel module
sudo ./scripts/load.sh
./scripts/confidence.sh          # verify + device suite
sudo ./scripts/unload.sh
```

See [GUIDE.md](GUIDE.md) for presets, sanitizer runs, and the full contract.

## Documentation

- [Architecture and contracts](docs/architecture.md) - file operations, readiness, reset, teardown
- [Control ABI](docs/abi.md) - ioctl semantics and statistics fields, version 5
- [Mmap observation transport](docs/mmap.md) - the snapshot contract and its limits
- [Validation strategy](docs/validation.md) - what runs on the host and what needs the VM
- [Measurements](docs/measurements.md) - benchmark method and labeled VM results
- [Interview notes](docs/interview-notes.md) - the decisions behind the design

## Limitations

- The queue is global and mutex-protected; it does not scale writers, and it
  makes no lock-free claim.
- The mmap page is an observation snapshot: no zero-copy consumption, no
  per-reader cursors, no eventfd notifications.
- The development loader sets `/dev/charlatan0` to mode `0666` inside the VM.
  A real deployment needs a udev rule and group ownership.
- Kernel lock debugging and kernel sanitizers depend on the target
  distribution kernel configuration and are not claimed here.

## License

Charlatan is released under the [MIT License](LICENSE).
