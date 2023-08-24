cmake_minimum_required(VERSION 3.26)
project(Table)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")

add_executable(Table main.cpp
        HashWrapper.h
        Table.h
)
