#!/bin/bash
set -e

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "Usage: $0 <interface> [timeout_sec]"
    echo "Example: $0 eth0 5"
    exit 1
fi

cd "$(dirname "$0")"
cmake -S .. -B ../build
cmake --build ../build
../bin/pcap_recv_demo live "$@"
