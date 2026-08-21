# Mmap decision

Charlatan deliberately has no mmap transport in this revision.

Mapping the current ring safely would require a separate ownership protocol for kernel producer, multiple competing readers, reset, and user-controlled tail advancement.

Exposing the writable ring without that protocol would be less correct than the syscall path and would not justify a zero-copy claim.

A future design can map a read-only metadata and event snapshot page, publish a monotonically increasing producer sequence with release semantics, and retain `poll` for notification.

That design would still need explicit overwrite detection and per-reader cursors.

It is a proposal, not a claimed feature or measurement.
