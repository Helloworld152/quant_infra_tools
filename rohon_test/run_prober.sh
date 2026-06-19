#!/bin/bash
set -e

cd "$(dirname "$0")"
export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH
mkdir -p flow_probe
echo "开始探测..."
../bin/auth_prober
