#cmake版本
cmake_minimum_required(VERSION 3.10)
project(mymoduo CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -pthread")

enable_testing()

add_subdirectory(base)
add_subdirectory(net)
add_subdirectory(base/test)
add_subdirectory(net/test)
add_subdirectory(example)

# 聚合入口（可选）
# add_executable(mymoduo_test test.cc)
# target_include_directories(mymoduo_test PRIVATE
#     ${CMAKE_SOURCE_DIR}/base
#     ${CMAKE_SOURCE_DIR}/net
# )
# target_link_libraries(mymoduo_test PRIVATE
#     mymoduo_base
#     mymoduo_net
# )

# add_test(NAME agg_main COMMAND mymoduo_test)

