#pragma once

#include "EventLoop.h"

#include <concepts>
#include <type_traits>

namespace mymoduo
{
namespace net
{
template<typename Func, typename callback>
concept AsyncTaskValid = 
    std::invocable<Func> && //无参可调用
    (
        std::same_as<std::invoke_result_t<Func>, void> &&
        std::invocable<callback> ||    // Func返回void 则 callback 无参可调用
        std::invocable<callback, std::invoke_result_t<Func>>   // Func返回非void 则 callback 可接收Func的返回值
    );   
class AsyncTaskScheduler : noncopyable
{
public:
    AsyncTaskScheduler(EventLoop* loop)
        :loop_(loop)
    {}

    template<typename Func, typename Callback>
    requires AsyncTaskValid<Func, Callback>
    void scheduleTask(Func&& func, Callback&& cb)
    {
        auto taskWrap = std::make_shared<AsyncTaskWrapper<Func, Callback>>(
            std::forward<Func>(func),
            std::forward<Callback>(cb)
        );
        loop_->queueInLoop([taskWrap]() {
            taskWrap->excute();
        });
    }

private:
    template<typename Func, typename Callback>
    requires AsyncTaskValid<Func, Callback>
    class AsyncTaskWrapper : noncopyable  //包装异步任务并获取其返回值
    {
    public:
        AsyncTaskWrapper(Func&& func, Callback&& cb)
            :func_(std::forward<Func>(func)),
             cb_(std::forward<Callback>(cb))
        {}

        void excute()
        {
            using ReturnType = std::invoke_result_t<Func>;
            if constexpr (std::is_void_v<ReturnType>)
            {
                func_();
                cb_();
            }else{
                //dectype 可以实现完美转发
                decltype(auto) result = func_();
                cb_(std::forward<decltype(result)>(result));
            }
        }
    private:
        Func func_;
        Callback cb_;
    };
    
    EventLoop* loop_;

};
} // namespace net
} // namespace mymoduo
