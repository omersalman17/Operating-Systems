cmake_minimum_required(VERSION 3.17)
project(ex4)

set(CMAKE_CXX_STANDARD 11)

add_executable(VirtualMemory VirtualMemory.cpp PhysicalMemory.cpp SimpleTest.cpp)
