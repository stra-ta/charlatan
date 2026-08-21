#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

[[ -c /dev/charlatan0 ]] || { printf '/dev/charlatan0 is not a character device\n' >&2; exit 1; }
"$root/build/userspace/charlatan-smoke" /dev/charlatan0
