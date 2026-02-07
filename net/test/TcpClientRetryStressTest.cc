#include "TcpClient.h"
#include "EventLoop.h"
#include "InetAddress.hpp"
#include "Logging.h" 
#include <future>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <iostream>
#include <atomic>

// 模拟大量的客户端同时尝试连接一个不存在的端口，触发重试
// 然后在重试过程中随机停止/销毁，验证 Connector 的资源管理是否安全
// 用于验证 fix: 修复 Connector 析构时可能发生的资源竞争和 use-after-free

void testConcurrentRetryAndDestruction()
{
    std::cout << "[TEST] Starting Concurrent Retry and Destruction Test..." << std::endl;
    // Set minimal logging to avoid spam, or enable DEBUG if needed to see crash stack more clearly
    // Logger::setLogLevel(Logger::LogLevel::WARN); 
    
    // 修复测试逻辑：EventLoop 必须在其运行的线程中创建
    // 这样 isInLoopThread() 才能正确工作
    std::atomic<mymoduo::net::EventLoop*> loopPtr{nullptr};
    std::promise<void> loopReady;
    std::future<void> loopReadyFuture = loopReady.get_future();

    std::thread loopThread([&](){
        mymoduo::net::EventLoop loop;
        loopPtr.store(&loop);
        loopReady.set_value();
        loop.loop();
    });

    loopReadyFuture.wait();
    mymoduo::net::EventLoop* loop = loopPtr.load();
    
    // Using a port that is likely closed.
    mymoduo::net::InetAddress serverAddr("127.0.0.1", 12345); 

    int clientCount = 100;
    std::vector<std::unique_ptr<mymoduo::net::TcpClient>> clients;

    // 1. Create and start clients
    std::cout << "[TEST] Creating " << clientCount << " clients connecting to closed port..." << std::endl;
    for(int i=0; i<clientCount; ++i) {
        std::string name = "Client-" + std::to_string(i);
        // Note: TcpClient is created in main thread, but uses loop from other thread.
        // This is valid as long as TcpClient handles thread safety (it mostly relies on loop->runInLoop)
        auto client = std::make_unique<mymoduo::net::TcpClient>(loop, serverAddr, name);
        
        // TcpClient starts connection with start(). 
        // Connector usually handles retry internally if connection fails.
        client->start(); 
        clients.push_back(std::move(client));
    }

    // 2. Run loop for a bit to allow retries to schedule
    
    std::cout << "[TEST] Clients started, waiting for retries..." << std::endl;
    // Wait enough time for retries to kick in (Connector usually backoffs)
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 3. Destruct/Stop clients randomly while loop is running
    std::cout << "[TEST] Destroying clients..." << std::endl;
    
    // Destroy half of them
    for(int i=0; i<clientCount/2; ++i) {
        clients.pop_back(); 
        // Add small random delays to hit different states (in callback, in timer, etc)
        if (i % 5 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Wait a bit more
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Destroy the rest
    clients.clear();

    std::cout << "[TEST] All clients destroyed." << std::endl;

    // 4. Stop loop
    loop->quit();
    if(loopThread.joinable()) {
        loopThread.join();
    }
    
    std::cout << "[TEST] Test passed (no crash)." << std::endl;
}

int main()
{
    testConcurrentRetryAndDestruction();
    return 0;
}
