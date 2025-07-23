cmake_minimum_required(VERSION 3.31)
project(beacon C)

set(CMAKE_C_STANDARD 11)

find_package(CURL REQUIRED)

add_executable(beacon main.c)

target_link_libraries(beacon PRIVATE curl cjson)
