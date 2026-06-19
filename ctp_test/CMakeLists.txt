cmake_minimum_required(VERSION 3.10)
project(CTP_API)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib)

# 添加可执行文件
add_executable(md_client src/md_client.cpp)
add_executable(query_instruments src/query_instruments.cpp)
add_executable(auth_test src/auth_test.cpp)
add_executable(for_quote_demo src/for_quote_demo.cpp)

# 链接库
target_link_libraries(md_client thostmduserapi_se pthread)

target_link_libraries(query_instruments 
    thosttraderapi_se
    pthread
)

target_link_libraries(auth_test
    thosttraderapi_se
    pthread
)

target_link_libraries(for_quote_demo
    thosttraderapi_se
    pthread
)
