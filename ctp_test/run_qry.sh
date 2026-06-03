#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
CONFIG_PATH="${1:-$SCRIPT_DIR/config/config.ini}"

cd "$BUILD_DIR"
cmake ..
make

LD_LIBRARY_PATH="$SCRIPT_DIR/lib:$LD_LIBRARY_PATH" ./bin/query_instruments "$CONFIG_PATH"
