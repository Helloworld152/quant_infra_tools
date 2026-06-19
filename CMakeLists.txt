cmake_minimum_required(VERSION 3.10)
project(ctp_api)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 统一将可执行文件输出到仓库根目录下的 bin，与 build 同级。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

add_subdirectory(ctp_test)
add_subdirectory(rohon_test)
