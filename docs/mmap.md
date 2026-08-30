# Mmap observation transport

Charlatan exposes one read-only page at `mmap(fd, 0, page_size, PROT_READ, MAP_SHARED, fd, 0)`.

The mapping is an observation transport, not a second queue-consumption API.

`read()` remains the only operation that removes events from the global FIFO.

## Page contract

The page contains one `struct charlatan_mmap_snapshot` from [the UAPI header](../include/uapi/charlatan.h).

It reports the current FIFO in logical order, from the oldest event at `events[0]` through `queue_depth` events.

The remaining event slots are zeroed after every publication.

The page reports `reset_generation`, the next event sequence, queue capacity, queue depth, and the ABI and event sizes.

The current page is one system page and the snapshot structure fits within a 4 KiB page.

## Consistent reads

The kernel uses a sequence counter around each copy.

It publishes an odd `version` before copying fields and an even `version` after the copy completes.

A userspace reader must load `version` with acquire semantics, copy the fields, load `version` again with acquire semantics, and accept the copy only when both values match and are even.

The helper used by the integration and benchmark clients is [mmap_snapshot.h](../userspace/src/mmap_snapshot.h).

The sequence counter protects readers from observing a partially copied event array.

It does not turn the page into a lock-free queue, and it does not give readers ownership of events.

## Ownership and permissions

The kernel owns the page and all writes to it.

The VMA rejects `PROT_WRITE`, clears `VM_MAYWRITE`, and marks the mapping non-expandable and non-dumpable.

The page is retained while a VMA exists through the VMA callbacks' module reference.

Closing the file descriptor does not make an existing mapping writable or change the snapshot contract.

The mapping is invalid for a nonzero file offset or a length other than one page.

## Notifications and performance

`poll()` and `epoll()` continue to report queue readiness.

The mmap page does not add an eventfd, because an eventfd would duplicate the existing readiness channel without solving ownership or wakeup policy.

The benchmark accepts `mmap` as a second mode:

```sh
./build/userspace/charlatan-bench 10000 syscall
./build/userspace/charlatan-bench 10000 mmap
```

Both modes write and consume one event per iteration.

The mmap mode also reads a consistent snapshot after each write, so its result measures snapshot observation plus the existing syscall path.

It is not a zero-copy consumption claim.

The benchmark output identifies the transport mode and must retain the commit and machine metadata supplied by the surrounding campaign runner.

The current repository has no Linux mmap benchmark result.
