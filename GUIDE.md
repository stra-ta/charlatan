# Charlatan guide

## What it is

A Linux character device, `/dev/charlatan0`, backed by a kernel module, plus a
C++20 userspace harness that drives it through real system calls.

- A global 128-slot FIFO of fixed-size events, protected by one mutex.
- Blocking and nonblocking `read()`, exact-size `write()`, `poll()`, and
  level-triggered `epoll()`.
- A versioned ioctl ABI, currently version 5: `RESET` and `GET_STATS`.
- Reset modeled with a per-descriptor generation so blocked readers receive
  `ECANCELED`.
- A read-only mmap page that publishes a sequence-checked observation snapshot.
- A full-event queue drops the newest event and reports it through `ENOSPC`
  and the `dropped` counter.

## Build

```sh
sudo ./scripts/install-deps.sh   # build-essential, cmake, kmod, pahole, kernel headers
./scripts/build.sh               # userspace harness (CMake) + kernel module (kbuild)
```

`build.sh` records the Ubuntu release, architecture, kernel, compiler, and
CMake versions in `build/environment.txt`, and refuses to run without kernel
headers for the running kernel. Set `CHARLATAN_KERNEL_CC` to override the
kernel compiler (`cc` by default; set a cross compiler only when building from
a different architecture).

Standard userspace preset:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

## Load, run, unload

```sh
sudo ./scripts/load.sh      # insmod + wait for /dev/charlatan0
./scripts/confidence.sh     # host verification + full device suite
sudo ./scripts/unload.sh    # refuses to unload while the device is open
```

`confidence.sh` runs the smoke, integration, stress, and both benchmark
transports, and skips the device part with a clear message when the module is
not loaded. Benchmark output can be written to JSON with
`./scripts/benchmark.sh 10000`.

The kernel module is built for the running kernel. Kernel-level verification
therefore happens inside a Linux VM or a Linux host with matching headers;
macOS hosts build and test only the userspace harness (CMake skips the
Linux-only binaries and CTest runs the smoke usage check).

## Sanitizers

```sh
cmake -S userspace -B build/asan-ubsan -DCMAKE_BUILD_TYPE=Debug -DCHARLATAN_SANITIZER=address,undefined
cmake --build build/asan-ubsan --parallel
sudo ./scripts/load.sh
ASAN_OPTIONS=detect_leaks=1 ./build/asan-ubsan/charlatan-tests
sudo ./scripts/unload.sh
```

`CHARLATAN_SANITIZER` also accepts `thread`. Kernel sanitizer and lock-debug
results depend on the distribution kernel configuration and are not claimed.

## Documentation

- [Architecture and contracts](docs/architecture.md) - operations, readiness, reset, teardown
- [Control ABI](docs/abi.md) - ioctl semantics and statistics fields
- [Validation strategy](docs/validation.md) - host vs VM coverage
- [Measurements](docs/measurements.md) - benchmark method and labeled VM results
- [Mmap observation transport](docs/mmap.md) - the snapshot contract
- [Interview notes](docs/interview-notes.md) - what the design decisions were for

## Limitations

- The queue is global and mutex-protected; there are no per-descriptor queues
  and no lock-free claims.
- The mmap page does not provide zero-copy consumption, per-reader cursors, or
  eventfd notifications.
- The development loader sets `/dev/charlatan0` to mode `0666`; a deployed
  device should use a least-privilege udev rule and group ownership.
- Benchmark values are evidence for the labeled VM, not bare-metal claims.

## License

Charlatan is released under the [MIT License](LICENSE).
