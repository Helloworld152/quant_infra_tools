#!/bin/bash
set -e

cd "$(dirname "$0")"
cmake -S .. -B ../build
cmake --build ../build
mkdir -p flow_md
LD_LIBRARY_PATH="$(pwd)/lib:$LD_LIBRARY_PATH" ../bin/md_client "$@"
