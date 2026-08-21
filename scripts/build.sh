#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="$root/build"
kernel_compiler=${CHARLATAN_KERNEL_CC:-aarch64-linux-gnu-gcc}

"$root/scripts/check-deps.sh"
mkdir -p "$build_dir"

{
    . /etc/os-release
    printf 'ubuntu=%s\n' "$PRETTY_NAME"
    printf 'architecture=%s\n' "$(uname -m)"
    printf 'kernel=%s\n' "$(uname -r)"
    g++ --version | { IFS= read -r version; printf 'gcc=%s\n' "$version"; }
    cmake --version | { IFS= read -r version; printf 'cmake=%s\n' "$version"; }
} > "$build_dir/environment.txt"

cmake -S "$root/userspace" -B "$build_dir/userspace" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build_dir/userspace" --parallel
make -C "/lib/modules/$(uname -r)/build" M="$root/kernel" CC="$kernel_compiler" modules
