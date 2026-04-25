#pragma once

#include "Callbacks.h"
#include "Channel.h"
#include "EventLoop.h"
#include "InetAddress.hpp"
#include "noncopyable.h"
#include "socket.h"

#include <algorithm>
#include <atomic>
#include <functional>

namespace mymoduo {
namespace net {
class Connector : noncopyable, public std::enable_shared_from_this<Connector> {
public:
  enum class StateE { kConnecting, kConnected, kDisconnecting, kDisconnected };
  using ConnectorCb = std::function<void(int sockfd)>;

  Connector(EventLoop *loop, const InetAddress &serverAdrr,
            int retryDelayMs = kInitRetryDelayMs);
  ~Connector();

  void start();       // 开启连接
  void restart();     // 重新连接
  void stop();        // 停止连接
  void stopAndWait(); // 同步停止连接，等待清理完成
  void setConnetionCb(ConnectorCb cb) { connetionCb_ = std::move(cb); }

private:
  static constexpr int kMaxRetryDelayMs = 30 * 1000; // 30s
  static constexpr int kInitRetryDelayMs = 500;      // 500ms

  void setState(StateE s) { state_.store(s, std::memory_order_release); }

  void startInLoop();
  void connecting(int sockfd);

  void retry(int sockfd); // 只有在kConnecting状态下才会调用
  void stopInLoop();

  int removeAndResetChannel();

  void connect();
  void handleWrite();
  void handleError();
  void resetChannel();

  EventLoop *loop_;
  InetAddress serverAddr_;
  std::atomic<bool> connect_;
  std::atomic<StateE> state_;
  std::unique_ptr<Channel> channel_;
  ConnectorCb connetionCb_;
  std::atomic<int> retryDelayMs_;
};
} // namespace net
} // namespace mymoduo
