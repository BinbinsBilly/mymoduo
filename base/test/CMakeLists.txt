# add_executable(base_logger_test
#     LoggerMain.cc
# )
# target_link_libraries(base_logger_test PRIVATE
#     mymoduo_base
# )
# target_include_directories(base_logger_test PRIVATE
#     ${CMAKE_SOURCE_DIR}/base
# )
#  #添加测试用例 #NAME base_logger：测试用例的名称 #COMMAND base_logger_test：测试用例执行的命令
# add_test(NAME base_logger COMMAND base_logger_test)
function(add_base_test test_name test_source)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PUBLIC
        mymoduo_base
            mymoduo_net
        )
    target_include_directories(${test_name} PUBLIC
        ${CMAKE_SOURCE_DIR}/base
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

function(add_base_test_hpp test_name test_source)
    add_executable()
endfunction()

add_base_test(base_logger_test LoggerMain.cc)
# add_base_test(atomic_tests AtomicTest.cc)
# add_base_test(mutexlock_tests MutexLockTest.cc)
# add_base_test(condition_tests ConditionTest.cc)
add_base_test(current_thread_tests CurrentThreadTest.cc)
add_base_test(current_thread_tests2 CurrentThreadTest2.cc)
add_base_test(thread_tests ThreadTest.cc)