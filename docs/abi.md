# Control ABI

The shared ABI is `include/uapi/charlatan.h`.

The ABI version is `5`.

Commands carrying a structure require its `abi_version` to match exactly.

Unknown commands return `ENOTTY`.

Malformed pointers return `EFAULT` when the kernel copies them.

| Command | Effect |
|---|---|
| `RESET` | Clear the queue, count discarded events, advance reset generation, wake waiters. |
| `GET_STATS` | Return a mutex-consistent snapshot. It is not a transactional history. |

## Version 5 change

Commands `0x03` through `0x07` (`SET_RATE`, `PAUSE`, `RESUME`,
`INJECT_OVERFLOW`, `SET_FAULTS`) were retired with the module's built-in
producer and fault countdowns. They now return `ENOTTY` like any unknown
command, and `struct charlatan_stats` dropped the producer, injection, and
fault fields. Events enter the queue through `write()` only.

## Statistics

`GET_STATS` reports a snapshot under the queue mutex, so every field describes
a single critical section.

| Field | Meaning |
|---|---|
| `produced` | Every accepted queue insertion. |
| `consumed` | Events removed after a successful copy to userspace. |
| `dropped` | Newest events rejected because the ring was full. |
| `reset_discards` | Events cleared by reset. |
| `resets` | Completed reset commands. |
| `queue_depth` | Queue depth at snapshot time, matching the mmap page. |
| `queue_capacity` | 128, the fixed ring size. |

All counters persist across `RESET` for the module lifetime only.

`reserved` is zero-filled padding that keeps the layout stable for future
fields.
