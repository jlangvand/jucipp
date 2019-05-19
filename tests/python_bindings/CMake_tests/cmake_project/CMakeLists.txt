cmake_minimum_required(VERSION 2.8)

project(cmake_project)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1y -Wall -Wextra")

add_executable(cmake_project main.cpp)
