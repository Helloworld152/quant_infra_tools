cmake_minimum_required(VERSION 3.10)
project(RohonTraderAuthDemo)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 库文件目录
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib)

# 添加可执行文件
add_executable(trader_auth_demo trader_auth_demo.cpp)
add_executable(auth_prober auth_prober.cpp)

# 链接库
target_link_libraries(trader_auth_demo 
    thosttraderapi_se
    rohonbase
    LinuxDataCollect
    pthread
)

target_link_libraries(auth_prober
    thosttraderapi_se
    rohonbase
    LinuxDataCollect
    pthread
)
