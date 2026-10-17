# Interview notes

The core decision was to use one mutex-protected ring rather than a lock-free
queue.

The device needs coherent reset, statistics, and FIFO behavior across
concurrent readers and writers.

The mutex makes the ownership transition explicit: an event is removed only
after `copy_to_user()` succeeds.

`poll` readiness is level-triggered because queue depth is the durable
condition.

Reset has a generation counter because waking a reader without a
distinguishable state change would leave it sleeping again on an empty queue.

The userspace harness is integration-first because the important contract is
the system-call boundary, not a mocked C++ abstraction.

Mmap was deferred rather than presented as zero-copy because a globally
consumed ring requires a separate shared ownership protocol.

The versioned ioctl ABI carries only reset and statistics: once events enter
through `write()` alone, every queue state is reachable from ordinary file
operations, and a control plane that duplicates them would be untested surface
area.

The primary limitation is that test permission mode is deliberately broad in
the VM.

A kernel-level detail worth knowing: the kernel calls `vm_ops->open` only when
a VMA is duplicated, never for the initial `mmap`, while `vm_ops->close` runs
on every teardown. The module therefore takes its page reference in the mmap
callback itself; anyone auditing module refcount bugs will look for exactly
that asymmetry.

A production deployment would use a udev rule and group ownership, and would
test against its target distribution kernel configuration.
