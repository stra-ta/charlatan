# Validation strategy

`charlatan-tests` is an integration harness against the loaded module.

It covers empty nonblocking reads, malformed read sizes, a safely invalid
read-only destination, event preservation after `EFAULT`, blocking reads woken
by writes, FIFO wraparound across the full ring, `poll` and level-triggered
`epoll` readiness, the reset priority notification and its one `ECANCELED`
acknowledgement per descriptor, full-queue `ENOSPC` accounting, `POLLOUT`
wakeup after a full queue drains, malformed and retired ioctls, and reset of a
blocked reader.

`charlatan-stress` runs two consumers, a writer, a resetter, and rapid
open/close traffic for two seconds and fails on any unexpected errno.

Run both through `./scripts/confidence.sh` with the module loaded.

Userspace accepts `-DCHARLATAN_SANITIZER=address,undefined` or `thread` in an
alternate CMake build directory.

Kernel-level verification needs a matching Linux VM or host: the module is
built for the running kernel, and the harness opens `/dev/charlatan0`.

Kernel sanitizer and lock-debug results depend on the distribution kernel
configuration and are not claimed without a matching enabled kernel.

Hosts without the module build `charlatan-smoke` and run the CTest smoke usage
check; the queue logic has no userspace simulation, deliberately, because the
contract under test is the system-call boundary.
