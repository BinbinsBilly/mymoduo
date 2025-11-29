// #pragma once 

// #include <mutex>
// #include <condition_variable>
// #include <atomic>

// namespace mymoduo
// {
// class CountDownLatch
// {   
// public:
//     CountDownLatch(int count)
//         :count_(count)
//     {}

//     ~CountDownLatch() = default;

//     void countDown() 
//     {
//         if(count_.fetch_sub(1, std::memory_order_acq_rel) == 1)
//         {
//             std::unique_lock<std::mutex>(mutex_);
//             cond_.notify_all();
//         }
//     }
//     int getCount() const
//     {
//         return count_.load(std::memory_order_acquire);
//     }
//     void wait()
//     {
//         std::unique_lock<std::mutex>(mutex_);
//         cond_.wait(mutex_, [this](){ return count_.load(std::memory_order_acquire) == 0; });
//     }
// private:
//     // mutable Mutex mutex_;
//     std::mutex mutex_;
//     std::condition_variable cond_;
//     std::atomic<int> count_;
// }; //end CountDownLatch

    
// } //end mymoduo