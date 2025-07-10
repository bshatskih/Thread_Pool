cmake_minimum_required(VERSION 3.22)
set(CMAKE_CXX_STANDARD 20) 

project(Thread_Pool)

add_executable(Thread_Pool main.cpp Logger.h thread_pool.cpp test/test_tasks.cpp)