#include "HeartBeat.h"

#include <algorithm>
#include <cmath>


namespace mymoduo
{
namespace net
{

HeartBeat::HeartBeat(EventLoop* loop, int slots, double timeout)
    :loop_(loop)
    ,slots_(slots)
    ,timeout_(timeout)
    ,wheel_(slots)
    ,currentSlot_(0)
{
    //每秒滴答一次的定时器延后到 start() 中启动:
    //构造函数中 weak_from_this 尚不可用, 无法安全捕获
    // std::cout << "HeartBeat constructed: slots_=" << slots_ << " timeout_=" << timeout_ << " currentSlot_=" << currentSlot_ << std::endl;
    // std::cout << "HeartBeat addr: this="<< this << " &slots="<< &slots_ << " &timeout="<< &timeout_ << " &wheel="<< &wheel_ << " &currentSlot="<< &currentSlot_ << std::endl;
}

void HeartBeat::start()
{
    // 定时器捕获 weak_ptr: HeartBeat 析构后 tick 不再触发悬垂 this
    std::weak_ptr<HeartBeat> weakSelf = shared_from_this();
    //每秒滴答一次
    // schedule tick every 1 second
    tickTimerSeq_ = loop_->runEvery(1.0, [weakSelf](){
        if (auto self = weakSelf.lock())
        {
            self->tick();
        }
    });
}


HeartBeat::~HeartBeat()
{
    //清理所有连接
    //由TcpServer或TcpClient清理连接
    //这里需要清理容器
    //析构容器时会自动调用析构函数
    //取消tick定时器: cancel内部经runInLoop转发, 跨线程安全
    //若取消不掉(如loop已退出), 定时器随TimerQueue析构销毁, 不再执行
    loop_->cancel(tickTimerSeq_);
}

void HeartBeat::update(const TcpConnPtr& newConn)
{
    // Ensure modifications happen in loop_ thread
    if(!loop_->isInLoopThread())
    {
        TcpConnPtr conn = newConn;
        loop_->queueInLoop([this, conn]() {
            this->update(conn);
        });
        return;
    }
    const TcpConnPtr& conn = newConn;
    auto it = conn2slots_.find(conn);
    //向上取整且至少为1: 修复timeout<1s时step为0导致连接立即超时; 非整数秒向上取整
    int step = std::max(1, static_cast<int>(std::ceil(timeout_)));
    int newslot = (currentSlot_ + step) % slots_;
    
    if(it != conn2slots_.end())
    {
        // 如果连接已经在目标槽位中（比如同一秒内有成千上万个包），无需重复擦除和插入
        if(it->second == newslot)
        {
            return;
        }
        // 已加入，先擦除
        wheel_[it->second].erase(conn);
    }
    //for test
    // std::cout << "HeartBeat::update - before insert use_count=" << (conn ? conn.use_count() : 0) << std::endl;
    // 计算新的超时时间（按秒为单位的槽位数）
    wheel_[newslot].insert(conn);
    //for test
    // std::cout << "HeartBeat::update - after insert conn use_count=" << (conn ? conn.use_count() : 0) << std::endl;
    conn2slots_[conn] = newslot;
    //for test
    // std::cout << "HeartBeat::update - after map assign conn use_count=" << (conn ? conn.use_count() : 0) << std::endl;
}

// 只移除槽位映射并从槽位中擦除 不关闭连接 连接不由HeartBeat管理
void HeartBeat::remove(const TcpConnPtr& conn)
{
    if(!loop_->isInLoopThread())
    {
        TcpConnPtr c = conn;
        loop_->queueInLoop([this, c]() {
            this->remove(c);
        });
        return;
    }
    auto it = conn2slots_.find(conn);
    if(it != conn2slots_.end())
    {
        wheel_[it->second].erase(conn);
        conn2slots_.erase(it);
    }
}


void HeartBeat::tick()
{
    //当前槽位的是超时的
    //for test
    // std::cout << "HeartBeat::tick - before: slots_=" << slots_ << " currentSlot_=" << currentSlot_ << std::endl;
    if (slots_ <= 0) {
        std::cerr << "HeartBeat::tick - invalid slots_=" << slots_ << ", skipping tick" << std::endl;
        return;
    }
    currentSlot_  = (currentSlot_ + 1) % slots_;
    auto &expiration = wheel_[currentSlot_];
    
    if(expiration.empty()) 
        return;

    for(const auto &it : expiration)
    {
        //超时连接 清理
        conn2slots_.erase(it);
        if(removeConnectionCb_) //一定要有回调函数 否则连接依旧存在
        {
            //for test
            // std::cout<< "HeartBeat::tick - removing connection use_count=" << (it ? it.use_count() : 0) << std::endl;
            removeConnectionCb_(it);
        }
    }
    //清空当前槽位
    expiration.clear();
}
    
} // namespace mymoduo
} // namespace net