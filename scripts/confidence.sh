#!/usr/bin/env bash
# Device confidence suite. Runs the host verification first, then the full
# loaded-device suite when /dev/charlatan0 exists (load the module with
# ./scripts/load.sh).
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/build/userspace"

"$root/scripts/verify.sh"

if [[ ! -c /dev/charlatan0 ]]; then
    printf 'device confidence suite skipped: /dev/charlatan0 is not loaded\n'
    exit 0
fi

printf '== smoke ==\n'
"$build/charlatan-smoke" /dev/charlatan0
printf '== integration ==\n'
"$build/charlatan-tests"
printf '== stress ==\n'
"$build/charlatan-stress"
printf '== benchmark: syscall ==\n'
"$build/charlatan-bench" "${CHARLATAN_BENCH_EVENTS:-10000}" syscall
printf '== benchmark: mmap ==\n'
"$build/charlatan-bench" "${CHARLATAN_BENCH_EVENTS:-10000}" mmap

printf 'device confidence suite passed\n'
