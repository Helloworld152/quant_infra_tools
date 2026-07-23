cmake_minimum_required(VERSION 3.10)
project(NettoolsDemos)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(multicast_recv_demo src/multicast_recv_demo.cpp)
target_include_directories(
    multicast_recv_demo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/hft-common/include
)
set_target_properties(multicast_recv_demo PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
target_link_libraries(multicast_recv_demo pthread)

find_library(
    PCAP_LIBRARY
    NAMES pcap libpcap.so.0.8
    PATHS /lib /lib/x86_64-linux-gnu /usr/lib /usr/lib/x86_64-linux-gnu
)
if(NOT PCAP_LIBRARY)
    message(FATAL_ERROR "libpcap library not found; cannot build pcap_recv_demo")
endif()

add_executable(pcap_recv_demo src/pcap_recv_demo.cpp)
target_link_libraries(pcap_recv_demo ${PCAP_LIBRARY} pthread)
