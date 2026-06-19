#!/bin/bash

set -e

cd "$(dirname "$0")"
cmake -S .. -B ../build
cmake --build ../build

# 可选参数：配置文件路径（默认使用 config/config.ini）
CONFIG_PATH=${1:-config/config.ini}

# CTP API 需在 flow 目录下写流控文件，须先创建目录
mkdir -p flow_auth_test

# 设置库路径并运行认证测试
LD_LIBRARY_PATH="$(pwd)/lib:$LD_LIBRARY_PATH" ../bin/auth_test "$CONFIG_PATH"
