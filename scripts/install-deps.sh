#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID} -eq 0 ]]; then
    apt-get update
    apt-get install --no-install-recommends -y build-essential cmake kmod pahole "linux-headers-$(uname -r)"
    exit 0
fi

exec sudo "$0" "$@"
