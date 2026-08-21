# Validation strategy

`charlatan-tests` is an integration harness against the loaded module.

It covers empty nonblocking reads, malformed read sizes, a safely invalid read-only destination, event preservation after `EFAULT`, blocking reads, FIFO wraparound, poll, level-triggered epoll, `POLLOUT` after a full queue drains, overflow accounting, malformed and unknown ioctls, deterministic read/write faults, autonomous production, pause, and reset of a blocked reader.

`charlatan-stress` runs the delayed producer at a 1 ms period alongside two consumers, a writer, a resetter, and rapid open/close traffic for two seconds.

The integration suite deterministically holds `PAUSE` before worker cancellation, verifies a concurrent `RESUME` is blocked by producer control, clears stale events, and then requires fresh production after both commands complete.

Run it repeatedly with `./scripts/stress.sh` while the module is loaded.

Userspace accepts `-DCHARLATAN_SANITIZER=address,undefined` or `thread` in an alternate CMake build directory.

Kernel sanitizer and lock-debug results depend on the distribution kernel configuration and are not claimed without a matching enabled kernel.
