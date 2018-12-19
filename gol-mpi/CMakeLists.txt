cmake_minimum_required(VERSION 2.8)
project(gol_mpi)

set(CMAKE_CXX_COMPILER /usr/lib64/openmpi/bin/mpic++)
set(CMAKE_C_COMPILER /usr/lib64/openmpi/bin/mpicc)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${gol_mpi_SOURCE_DIR}/bin)

add_executable(gol_mpi main.cpp game_of_life.cpp)
