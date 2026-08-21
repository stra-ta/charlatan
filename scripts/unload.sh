#!/usr/bin/env bash
set -euo pipefail

module="charlatan"
device="/dev/charlatan0"

if [[ ${EUID} -ne 0 ]]; then
    printf 'run as root: sudo %s\n' "$0" >&2
    exit 2
fi

if [[ ! -d "/sys/module/$module" ]]; then
    printf '%s is not loaded\n' "$module"
    exit 0
fi

shopt -s nullglob
for fd in /proc/[0-9]*/fd/*; do
    target=$(readlink "$fd") || continue
    if [[ "$target" == "$device" ]]; then
        printf 'refusing unload: %s is still open (%s)\n' "$device" "$fd" >&2
        exit 1
    fi
done

rmmod "$module"
