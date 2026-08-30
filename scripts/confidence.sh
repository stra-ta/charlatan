#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

"$root/scripts/verify.sh"

if [[ ! -c /dev/charlatan0 ]]; then
    printf 'device confidence suite skipped: /dev/charlatan0 is not loaded\n'
    exit 0
fi

"$root/scripts/smoke.sh"
"$root/scripts/test-integration.sh"
"$root/scripts/stress.sh"
"$root/scripts/benchmark.sh" "${CHARLATAN_BENCH_EVENTS:-10000}"
"$root/build/userspace/charlatan-bench" "${CHARLATAN_BENCH_EVENTS:-10000}" mmap

printf 'device confidence suite passed\n'
