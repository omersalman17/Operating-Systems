cmake_minimum_required(VERSION 3.17)
project(ex2)

set(CMAKE_CXX_STANDARD 11)

add_executable(uthreads uthreads.cpp thread.cpp)

add_executable(main main.cpp)
