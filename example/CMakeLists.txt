cmake_minimum_required(VERSION 3.16)
project(mymoduo_examples CXX)

add_executable(pingpong_bench pingpong_bench.cc)
target_link_libraries(pingpong_bench PRIVATE mymoduo_net mymoduo_base)

