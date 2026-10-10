cmake_minimum_required(VERSION 3.20)

project(mythreadpool
    VERSION 0.2.0
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(myhandwrite
    myHandWriteVersion/threadpool.h
    myHandWriteVersion/threadpool.cpp
    myHandWriteVersion/using_example.cpp
)
target_include_directories(myhandwrite PRIVATE myHandWriteVersion)

add_executable(modern_threadpool
    modernCppVersion/threadpool.h
    modernCppVersion/using_example.cpp
)
target_include_directories(modern_threadpool PRIVATE modernCppVersion)

if (NOT MSVC)
    target_compile_options(myhandwrite PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(modern_threadpool PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_options(myhandwrite PRIVATE -static-libgcc -static-libstdc++)
target_link_options(modern_threadpool PRIVATE -static-libgcc -static-libstdc++)
