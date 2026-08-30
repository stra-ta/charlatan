#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="${CHARLATAN_VERIFY_BUILD_DIR:-$root/build/verify}"

cmake -S "$root/userspace" -B "$build_dir" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure

printf 'userspace verification passed: %s\n' "$build_dir"
