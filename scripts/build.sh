#!/usr/bin/env bash
# Build the userspace harness and the kernel module.
# The module requires kernel headers for the running kernel; run this inside
# the Linux VM (or any Linux host) with ./scripts/install-deps.sh satisfied.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="$root/build"
kernel_compiler=${CHARLATAN_KERNEL_CC:-cc}

headers="/lib/modules/$(uname -r)/build"
if [[ ! -d "$headers" ]]; then
    printf 'missing kernel headers: %s\n' "$headers" >&2
    printf 'install dependencies with: sudo ./scripts/install-deps.sh\n' >&2
    exit 1
fi
for command in cmake g++ make "$kernel_compiler"; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'missing command: %s\n' "$command" >&2
        printf 'install dependencies with: sudo ./scripts/install-deps.sh\n' >&2
        exit 1
    fi
done

mkdir -p "$build_dir"

{
    . /etc/os-release
    printf 'ubuntu=%s\n' "${PRETTY_NAME:-unknown}"
    printf 'architecture=%s\n' "$(uname -m)"
    printf 'kernel=%s\n' "$(uname -r)"
    g++ --version | { IFS= read -r version; printf 'gcc=%s\n' "$version"; }
    cmake --version | { IFS= read -r version; printf 'cmake=%s\n' "$version"; }
} > "$build_dir/environment.txt"

cmake -S "$root/userspace" -B "$build_dir/userspace" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build_dir/userspace" --parallel
make -C "$headers" M="$root/kernel" CC="$kernel_compiler" modules
