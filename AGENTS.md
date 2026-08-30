# Charlatan contributor guide

## Scope

Charlatan is a Linux kernel character device with a C++20 userspace harness.

The queue is global, bounded to 128 events, mutex-protected, and consumed only by `read()`.

The mmap interface publishes a read-only snapshot for observation.

It does not grant userspace ownership of queue slots or change `read()` semantics.

## Invariants

- `include/uapi/charlatan.h` is the shared ABI and every layout change increments the relevant ABI version.
- `read()` consumes FIFO events only after `copy_to_user()` succeeds.
- Reset advances the per-open generation and wakes blocked readers.
- The mmap snapshot uses an odd/even sequence counter and readers accept only a stable even version.
- Kernel claims require a matching Linux VM and kernel headers.
- VM benchmark values are evidence for that VM, not bare-metal performance claims.

## Commands

Run host userspace checks with `./scripts/verify.sh`.

Run `./scripts/confidence.sh` for the host checks plus loaded-device integration, stress, and both benchmark transports.

Build the full ARM64 VM artifact with `./scripts/build.sh`.

Run `cmake --preset debug`, `cmake --build --preset debug`, and `ctest --preset debug` for the standard userspace preset.

Use `cmake --preset asan-ubsan` for the userspace sanitizer build.

Load and unload the module only through `scripts/load.sh` and `scripts/unload.sh`.

## Verification

Changes to kernel operations need a regression in `userspace/src/integration.cpp` or a documented reason why the VM-only boundary cannot run locally.

Changes to the UAPI need compile coverage for both kernel and userspace consumers.

Benchmark output must identify its transport and retain the surrounding campaign provenance.


## Lab-wide contracts

- See https://github.com/stra-ta/.github/blob/main/LAB_RULES.md and https://github.com/stra-ta/.github/blob/main/EVIDENCE.md and https://github.com/stra-ta/.github/blob/main/COMPATIBILITY.md for lab-wide naming, evidence, and schema contracts.
- Per https://github.com/stra-ta/.github/blob/main/CONTRIBUTING.md, contributions require the target repo's AGENTS.md, README, and relevant design note, preserve repo boundaries, add the narrowest regression test, run one-command verification, and keep performance claims tied to committed manifests.
