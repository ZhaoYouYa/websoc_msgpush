cmake_minimum_required(VERSION 3.24.2)
project(SilencedWebSocket VERSION 0.1.0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
find_package(asio CONFIG REQUIRED)
find_package(Drogon CONFIG REQUIRED)
find_package(Boost REQUIRED log)

add_executable(SilencedWebSocket
log.h
log.cpp
server.h
main.cpp
)
target_link_libraries(SilencedWebSocket PRIVATE asio asio::asio ${DROGON_LIBRARIES}  Boost::boost Boost::log)


