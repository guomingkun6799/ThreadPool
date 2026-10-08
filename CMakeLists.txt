cmake_minimum_required(VERSION 3.20)

project(mythreadpool
    VERSION 0.1.0
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(mythreadpool
    threadpool.cpp
    threadpool.h
)

target_compile_options(mythreadpool PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(mythreadpool PRIVATE -static-libgcc -static-libstdc++)
