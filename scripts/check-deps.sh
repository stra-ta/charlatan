#!/usr/bin/env bash
set -euo pipefail

missing=0

for command in aarch64-linux-gnu-gcc cmake g++ make pahole; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'missing command: %s\n' "$command" >&2
        missing=1
    fi
done

headers="/lib/modules/$(uname -r)/build"
if [[ ! -d "$headers" ]]; then
    printf 'missing kernel headers: %s\n' "$headers" >&2
    missing=1
fi

if (( missing )); then
    printf 'Install the missing packages with: sudo apt-get install build-essential cmake linux-headers-$(uname -r) kmod\n' >&2
    exit 1
fi

printf 'dependencies available for kernel %s\n' "$(uname -r)"
