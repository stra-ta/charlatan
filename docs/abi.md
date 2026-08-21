# Control ABI

The shared ABI is `include/uapi/charlatan.h`.

The ABI version is `4`.

Commands carrying a structure require its `abi_version` to match exactly.

Unknown commands return `ENOTTY`.

Malformed pointers return `EFAULT` when the kernel copies them.

| Command | Effect |
|---|---|
| `RESET` | Clear the queue, count discarded events, advance reset generation, wake waiters. |
| `GET_STATS` | Return a mutex-consistent snapshot. It is not a transactional history. |
| `SET_RATE` | Set a producer period from 0 to 60,000 ms. Zero stops production. |
| `PAUSE` | Stop scheduled production while retaining the configured period. |
| `RESUME` | Restart production at the configured nonzero period. |
| `INJECT_OVERFLOW` | Attempt to enqueue up to 100,000 deterministic synthetic events. Excess events follow the normal drop-newest policy. |
| `SET_FAULTS` | Set `EIO` countdowns and an optional 0 to 1,000 ms producer-control delay for deterministic control-plane race tests. |

Producer-control commands are serialized.

Each successful `SET_RATE`, `PAUSE`, or `RESUME` completes with the matching worker state installed, even when callers issue those commands concurrently.

`producer_control_delay_ms` is sampled by `SET_RATE` and `PAUSE` after their state update and before cancellation.

`RESUME` is not delayed.

The producer is intentionally disabled at module load for deterministic tests.

`GET_STATS` reports a snapshot under the queue mutex, so every field describes a single critical section.

`produced` counts every accepted queue insertion, including manual writes and delayed-worker events.

`injected` is the accepted subset created by write or overflow injection.

`consumed` counts events removed after a successful copy to userspace.

`dropped` counts newest events rejected because the ring was full.

`reset_discards` counts events cleared by reset, and `resets` counts completed reset commands.

`read_failures` and `write_failures` count the `EIO` responses produced by fault countdowns.

`producer_running` reports whether the delayed producer is enabled in the same mutex-consistent snapshot.

`producer_control_waiters` counts control ioctls that have entered the producer-control path.

`producer_control_delay_active` reports an active deterministic delay between the `SET_RATE` or `PAUSE` state update and cancellation.

All counters persist across `RESET` and module lifetime only.

Fault countdowns are global to the device, not per descriptor.

They are consumed before empty-queue, reset-generation, or full-queue checks, but after read-size validation and write user-copy validation.

Fault configuration persists across reset and does not change poll readiness until the affected operation runs.
