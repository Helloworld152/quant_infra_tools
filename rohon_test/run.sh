#!/bin/bash
set -e

cd "$(dirname "$0")"

# 设置库路径
export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH

# 运行程序
../bin/trader_auth_demo
