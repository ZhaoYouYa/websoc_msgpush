cmake_minimum_required(VERSION 3.16.3)
project(SilencedWebSocket VERSION 0.1.0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
find_package(asio CONFIG REQUIRED)
find_package(Drogon CONFIG REQUIRED)

add_executable(SilencedWebSocket
zyyservercommon.h
main.cpp
)
target_link_libraries(SilencedWebSocket PRIVATE asio asio::asio ${DROGON_LIBRARIES})


