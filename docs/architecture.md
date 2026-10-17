# Architecture and contracts

```text
C++20 harness -- read/write/ioctl/poll/epoll --> /dev/charlatan0
                                                    |
                                             mutex-protected ring
                                             wait queue
                                                    |
                                      read-only mmap observation page
```

The ring has 128 fixed-size `charlatan_event` slots.

The queue is global, not per file descriptor.

Competing readers consume from the same FIFO, and writers serialize through one mutex.

The implementation chooses an ordinary mutex and wait queue over lock-free
state because reset, statistics, and FIFO ownership transitions must observe
one coherent queue state.

The mmap path publishes a copy of the queue for observation through a
sequence counter, and it does not replace the mutex-protected FIFO.

## File operation contract

- `open()` allocates reader-local reset state and does not clear the global queue.
- Any number of readers and writers may open the node.
- Each event is consumed by at most one reader.
- `read()` requires a buffer that is a nonzero multiple of 24 bytes.
- A read returns one to 64 events, up to the caller's capacity, and preserves FIFO order.
- An empty blocking read waits until an event, a reset, or a signal arrives.
- An empty nonblocking read returns `EAGAIN`.
- A reset is observed once per open descriptor as `ECANCELED`, including by a blocked reader.
- A failed first `copy_to_user()` returns `EFAULT` without consuming an event.
- `write()` accepts exactly one 24-byte event and uses only its `value` and `flags` fields.
- A full queue drops the newest incoming event, increments `dropped`, and makes `write()` return `ENOSPC`.
- `poll()` is level-triggered.
- `POLLIN` remains set while the queue is nonempty.
- `POLLOUT` remains set while queue depth is below capacity.
- A reset is exposed as `POLLPRI` until that descriptor observes its one `ECANCELED` read result.
- Draining a full queue wakes write waiters.
- `epoll` uses the caller's selected mode, with the integration harness using default level-triggered mode.
- `close()` releases only its reader-local state.
- Closing a descriptor from another userspace thread is not a cancellation API for a blocked read.
- The VFS module reference prevents normal module unload while the device is open.

## State transitions

```text
empty --enqueue--> nonempty --read--> empty
nonempty --enqueue--> full --read--> nonempty
any --reset--> empty + generation advance
```

`reset` discards queued events, increments the reset generation, and wakes waiters.

Multiple resets before a descriptor reads are coalesced into one `ECANCELED` observation.

## Teardown

Module exit marks shutdown, wakes waiters, destroys the device and
character-device registration, then releases the snapshot page.

The supplied unload script also refuses unload if `/proc/*/fd` shows an open device descriptor.

## What is not here

The module has no built-in event producer and no fault-injection commands.
Both existed in ABI version 4 to make the device self-exercising; the harness
now drives every queue state through `write()`, `read()`, reset, and mmap
directly, so the device surface is limited to what the file operations expose.
