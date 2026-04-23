#cmake版本
cmake_minimum_required(VERSION 3.10)
project(mymoduo CXX)

# 设置默认构建动态库 (Shared Library)
set(BUILD_SHARED_LIBS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread -Wno-unused-variable -Wno-unused-but-set-variable")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fno-omit-frame-pointer")

enable_testing()

add_subdirectory(base)
add_subdirectory(net)
add_subdirectory(base/test)
add_subdirectory(net/test)
add_subdirectory(example)

# 3. 安装目标
install(TARGETS mymoduo_net mymoduo_base
    EXPORT mymoduo-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include   # 给使用者自动传递 -I<prefix>/include
)

# 4. 安装头文件（去掉双层 include）
# 原：install(DIRECTORY ${CMAKE_SOURCE_DIR}/net/include DESTINATION include)
# 改：把 include 目录下的“内容”拷到 <prefix>/include，而不是再套一层 include
install(DIRECTORY ${CMAKE_SOURCE_DIR}/net/include/
        DESTINATION include
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
install(DIRECTORY ${CMAKE_SOURCE_DIR}/base/include/
        DESTINATION include
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")

# 5. 导出配置（不再硬编码 /opt/mymoduo）
install(EXPORT mymoduo-targets
    FILE mymoduo-config.cmake
    NAMESPACE mymoduo::
    DESTINATION lib/cmake/mymoduo   # 跟随 CMAKE_INSTALL_PREFIX
)
# # 3. 安装目标
# install(TARGETS mymoduo_net mymoduo_base
#     EXPORT mymoduo-targets
#     ARCHIVE DESTINATION lib
#     LIBRARY DESTINATION lib
#     RUNTIME DESTINATION bin
#     INCLUDES DESTINATION include  # ← 自动传递头文件路径
# )
# # 4. 安装头文件
# install(DIRECTORY ${CMAKE_SOURCE_DIR}/net/include DESTINATION include)
# install(DIRECTORY ${CMAKE_SOURCE_DIR}/base/include DESTINATION include)
# # 5. 导出配置（这个会自动生成正确的配置）
# install(EXPORT mymoduo-targets
#     FILE mymoduo-config.cmake
#     NAMESPACE mymoduo::
#     DESTINATION /opt/mymoduo
# )

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

