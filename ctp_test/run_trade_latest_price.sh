#!/bin/bash
set -e

cd "$(dirname "$0")"
cmake -S .. -B ../build
cmake --build ../build --target trade_latest_price

CONFIG_PATH=${1:-config/config.ini}

mkdir -p flow_trade_latest_price

LD_LIBRARY_PATH="$(pwd)/lib:$LD_LIBRARY_PATH" ../bin/trade_latest_price "$CONFIG_PATH"
