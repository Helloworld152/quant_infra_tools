#!/bin/bash

set -e

if [ $# -lt 2 ]; then
    echo "Usage: $0 <instrument_id> <exchange_id> [config_path]"
    echo "Example: $0 au2412 SHFE config/config.ini"
    exit 1
fi

cd "$(dirname "$0")"
cmake -S .. -B ../build
cmake --build ../build

INSTRUMENT_ID=$1
EXCHANGE_ID=$2
CONFIG_PATH=${3:-config/config.ini}

mkdir -p flow_for_quote

LD_LIBRARY_PATH="$(pwd)/lib:$LD_LIBRARY_PATH" ../bin/for_quote_demo "$INSTRUMENT_ID" "$EXCHANGE_ID" "$CONFIG_PATH"
