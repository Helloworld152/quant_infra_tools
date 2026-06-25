#!/bin/bash
set -e

if [ $# -ne 1 ]; then
    echo "Usage: $0 <pcap_file>"
    echo "Example: $0 /tmp/sample.pcap"
    exit 1
fi

cd "$(dirname "$0")"
cmake -S .. -B ../build
cmake --build ../build
../bin/pcap_recv_demo file "$1"
