# add_executable(net_tests
#     BufferTest.cc
# )
# target_link_libraries(net_tests PRIVATE
#     mymoduo_net
#     mymoduo_base
# )
# target_include_directories(net_tests PRIVATE
#     ${CMAKE_SOURCE_DIR}/net
#     ${CMAKE_SOURCE_DIR}/base
# )
# # 添加测试用例 #NAME buffer_basic：测试用例的名称  #COMMAND net_tests：测试用例执行的命令
# add_test(NAME buffer_basic COMMAND net_tests --gtest_filter=BufferTest.*)

#每一个测试对应一个可执行文件
function(add_net_test test_name test_source)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PUBLIC
        mymoduo_net
        mymoduo_base
    )
    target_include_directories(${test_name} PUBLIC
        ${CMAKE_SOURCE_DIR}/net
        ${CMAKE_SOURCE_DIR}/base
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()


add_net_test(net_buffer_tests BufferTest.cc)
add_net_test(net_inetaddress_tests InetAddressTest.cc)
add_net_test(net_socket_tests SocketTest.cc)
add_net_test(net_timer_tests TimerTest.cc)
add_net_test(net_channel_tests ChannelTest.cc)
add_net_test(net_timerqueue_heap_unique_ptr_pmr_tests TimerQueueHeapUniquePtrPmrTest.cc)
add_net_test(net_epollpoller_tests EpollPollerTest.cc)
add_net_test(net_timerqueue_integration_tests TimerQueueIntegrationTest.cc)
add_net_test(net_eventloop_basic_tests EventLoopBasicTest.cc)
add_net_test(net_eventloopthread_tests EventLoopThreadTest.cc)
add_net_test(net_eventloopthreadpool_tests EventLoopThreadPoolTest.cc)
add_net_test(net_asynctaskscheduler_tests AsyncTaskSchedulerTest.cc)
# add_net_test(net_tcpserver_tests TcpServerTest.cc)
add_net_test(net_tcpconn_tests TcpConnTest.cc)
add_net_test(net_connector_tests ConnectorTest.cc)
add_net_test(net_tcpclient_tests TcpClientTest.cc)
