#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
module="charlatan"

if [[ ${EUID} -ne 0 ]]; then
    printf 'run as root: sudo %s\n' "$0" >&2
    exit 2
fi

if [[ -d "/sys/module/$module" ]]; then
    printf '%s is already loaded; refusing to replace it\n' "$module" >&2
    exit 1
fi

if [[ ! -f "$root/kernel/$module.ko" ]]; then
    printf 'module missing: run ./scripts/build.sh first\n' >&2
    exit 1
fi

insmod "$root/kernel/$module.ko"

for _ in {1..20}; do
    if [[ -c /dev/charlatan0 ]]; then
        chmod 0666 /dev/charlatan0
        exit 0
    fi
    sleep 0.05
done

printf 'module loaded but /dev/charlatan0 was not created\n' >&2
exit 1
