add_library(mymoduo_net
    Acceptor.cc
    Buffer.cc
    Connector.cc
    TcpClient.cc
    socket.cpp
    timer.cc
    Channel.cc
    TimerQueue.cc
    TimerQueueHeapUniquePtrPmr.cc
    Poller.cc
    EpollPoller.cc
    EventLoop.cc
    EventLoopThread.cc
    EventLoopThreadPool.cc
    TcpConn.cc
    TcpServer.cc
    HeartBeat.cc
)
# target_include_directories(mymoduo_net PUBLIC
#     ${CMAKE_CURRENT_SOURCE_DIR}/include
#     ${CMAKE_SOURCE_DIR}/base/include
# )

target_include_directories(mymoduo_net PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/base/include>
    $<INSTALL_INTERFACE:include>
)

