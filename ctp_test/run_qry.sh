#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG_PATH="${1:-$SCRIPT_DIR/config/config.ini}"

cd "$SCRIPT_DIR"
cmake -S .. -B ../build
cmake --build ../build
LD_LIBRARY_PATH="$SCRIPT_DIR/lib:$LD_LIBRARY_PATH" ../bin/query_instruments "$CONFIG_PATH"
