cmake_minimum_required(VERSION 3.22.1)
project(websoc_msgpush VERSION 0.1.0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
find_package(Drogon CONFIG REQUIRED)
add_executable(websoc_msgpush
sl_tcpconnect.hpp
main.cpp
)
target_link_libraries(websoc_msgpush  ${DROGON_LIBRARIES})


