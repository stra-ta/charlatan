#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$root/results"
"$root/build/userspace/charlatan-bench" "${1:-10000}" | tee "$root/results/stream.json"
