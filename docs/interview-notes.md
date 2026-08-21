# Interview notes

The core decision was to use one mutex-protected ring rather than a lock-free queue.

The device needs coherent reset, fault, statistics, and FIFO behavior across concurrent readers and writers.

The mutex makes the ownership transition explicit: an event is removed only after `copy_to_user()` succeeds.

`poll` readiness is level-triggered because queue depth is the durable condition.

Reset has a generation counter because waking a reader without a distinguishable state change would leave it sleeping again on an empty queue.

The userspace harness is integration-first because the important contract is the system-call boundary, not a mocked C++ abstraction.

Mmap was deferred rather than presented as zero-copy because a globally consumed ring requires a separate shared ownership protocol.

The primary limitation is that test permission mode is deliberately broad in the VM.

A production deployment would use a udev rule and group ownership, and would test against its target distribution kernel configuration.
