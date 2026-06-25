#!/bin/bash
set -e

if [ $# -lt 2 ] || [ $# -gt 4 ]; then
    echo "Usage: $0 <multicast_ip> <port> [bind_ip] [timeout_sec]"
    echo "Example: $0 239.10.10.10 12345 0.0.0.0 5"
    exit 1
fi

cd "$(dirname "$0")"
cmake -S .. -B ../build
cmake --build ../build
../bin/multicast_recv_demo "$@"
