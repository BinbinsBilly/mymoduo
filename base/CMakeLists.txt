add_library(mymoduo_base
    Logging.cc
    LogStream.cc
    TimeStamp.cc
    # Condition.cc
    CurrentThread.cc
    Thread.cc
    #INTERFACE Atomic.hpp  不需要包含 cmake会自动处理普通头文件 (这不属于接口库->只含有头文件的库才用INTERFACE)
)
# target_include_directories(mymoduo_base PUBLIC
#     ${CMAKE_CURRENT_SOURCE_DIR}/include
# )

target_include_directories(mymoduo_base PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)