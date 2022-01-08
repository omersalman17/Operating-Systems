cmake_minimum_required(VERSION 3.17)
project(ex3)

set(CMAKE_CXX_STANDARD 11)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

add_compile_options(-pthread)


add_executable(MapReduceFramework MapReduceFramework.cpp Barrier.cpp SampleClient.cpp)
