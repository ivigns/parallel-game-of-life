cmake_minimum_required(VERSION 3.13)
project(gol_pthread)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

add_executable(gol_pthread main.cpp game_of_life.cpp multithreading_utils.cpp)
