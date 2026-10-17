# Charlatan contributor guide

## Scope

Charlatan is a Linux kernel character device (`/dev/charlatan0`) with a C++20
userspace harness.

The queue is global, bounded to 128 events, mutex-protected, and consumed only
by `read()`.

The ioctl ABI carries `RESET` and `GET_STATS`; there is no built-in producer
and no fault injection, by design: every queue state is reachable through
`write()`, `read()`, reset, and mmap.

The mmap interface publishes a read-only snapshot for observation.

It does not grant userspace ownership of queue slots or change `read()` semantics.

## Invariants

- `include/uapi/charlatan.h` is the shared ABI and every layout change increments the relevant ABI version (currently 5).
- `read()` consumes FIFO events only after `copy_to_user()` succeeds.
- Reset advances the per-open generation and wakes blocked readers.
- The mmap snapshot uses an odd/even sequence counter and readers accept only a stable even version.
- Kernel claims require a matching Linux VM and kernel headers.
- VM benchmark values are evidence for that VM, not bare-metal performance claims.

## Commands

Run host userspace checks with `./scripts/verify.sh`.

Run `./scripts/confidence.sh` for the host checks plus the loaded-device
suite (smoke, integration, stress, both benchmark transports).

Build the userspace harness and the kernel module with `./scripts/build.sh`
inside the VM or a Linux host with matching headers.

Run `cmake --preset debug`, `cmake --build --preset debug`, and
`ctest --preset debug` for the standard userspace preset.

Use `-DCHARLATAN_SANITIZER=address,undefined` (or `thread`) for the sanitizer
build.

Load and unload the module only through `scripts/load.sh` and `scripts/unload.sh`.

## Verification

Changes to kernel operations need a regression in `userspace/src/integration.cpp`
or a documented reason why the VM-only boundary cannot run locally.

Changes to the UAPI need compile coverage for both kernel and userspace consumers.

Benchmark output must identify its transport and retain its environment label.

Deleting control-plane surface is preferred over keeping untested commands:
if a command cannot be exercised from the harness, it should not exist.
