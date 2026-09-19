#include "TcpClient.h"
#include "TcpConn.h"
#include <chrono>
#include <future>

namespace mymoduo {
namespace net {
namespace detail {} // end namespace detail

TcpClient::TcpClient(EventLoop *loop, const InetAddress &serverAddr,
                     const std::string name)
    : loop_(loop), serverAddr_(serverAddr), name_(name), connect_(false),
      retry_(false), connector_(std::make_shared<Connector>(loop, serverAddr)),
      tcpConnetion_(nullptr) {
  connector_->setConnetionCb([this](int sockfd) { this->connection(sockfd); });
}

TcpClient::~TcpClient() {
  LOG_DEBUG << "TcpClient::~TcpClient - TcpClient destructing";
  // Stop retry first to prevent new connections from being established
  retry_ = false;
  connect_ = false;

  // Break callback dependency on this object before stopping connector.
  // 防止析构时, 还有连接进入
  connector_->setConnetionCb(Connector::ConnectorCb{});

  // Synchronously stop the connector to prevent new connections
  // from being established during destruction (e.g. retry timers).
  connector_->stopAndWait();

  TcpConnPtr conn{nullptr};
  {
    std::unique_lock lock(shared_mutex_);
    conn = std::move(tcpConnetion_);
  }
  LOG_DEBUG << "std::move(tcpConnetion_) done in ~TcpClient";
  if (conn) {
    // Synchronously destroy the connection in its loop thread to ensure
    // all callbacks (onConnection DOWN, handleClose, removeConnection)
    // complete before TcpClient is fully destroyed.
    // This prevents use-after-free in user callbacks that capture `this`.
    EventLoop *ioLoop = conn->getLoop();
    // promise 用 shared_ptr: 超时返回后 functor 仍可能迟到执行, 不能引用栈上对象
    // functor 捕获 conn(shared_ptr), 迟到执行也是安全的
    auto p = std::make_shared<std::promise<void>>();
    auto f = p->get_future();
    ioLoop->runInLoop([conn, p]() {
      conn->connectDestroyed();
      p->set_value();
    });
    // 有界等待: loop 已 quit 时 functor 永不执行, 无界等待会导致析构死锁
    if (f.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
      LOG_WARN << "TcpClient::~TcpClient - timeout waiting for connection "
                  "destruction, loop may have stopped";
    }
    LOG_DEBUG << "TcpConn destroyed in ~TcpClient";
  }
}

bool TcpClient::isConnected() const {
  std::shared_lock lock(shared_mutex_);
  return tcpConnetion_ && tcpConnetion_->connected();
}

void TcpClient::start() {
  if (!connect_.load(std::memory_order_acquire)) {
    connect_ = true;
    connector_->start(); // 开始连接到server
  } else {
    LOG_ERROR << "already connecting, connect_ != false";
  }
}

// 发起连接请求期间 关闭连接请求
void TcpClient::stop() {
  if (connect_.load(std::memory_order_acquire)) {
    connect_ = false;
    retry_ = false;     // 停止重试
    connector_->stop(); // 停止连接  如何retry_ == false 则不会重试

    // 这里只是关闭连接请求, 若retry_ == true 会重试.  因此不能关闭connection
    // 因此在removeConnection回调时若重新建立了连接请求, removeConnetion退出时,
    // event!=KNoneEvent, 会断言失败
    // 关闭connection要调用disconnect
  }
}

void TcpClient::disconnect() {
  retry_ = false; // 关闭重试
  {
    std::shared_lock lock(shared_mutex_);
    if (tcpConnetion_) {
      // 关闭写端
      tcpConnetion_->shutdown();
      // 关闭写端后就会调用 handleClose 进而调用removeConnection
    }
  }
}

void TcpClient::removeConnection() {
  TcpConnPtr conntemp;
  {
    // 这里不用assert
    // 因为TcpConn不属于某个loop, 只是TcpConn中的channel属于某个loop
    std::unique_lock lock(shared_mutex_);
    if (tcpConnetion_) {
      conntemp = std::move(tcpConnetion_);
    }
  }
  // Only schedule connectDestroyed if we actually have a connection.
  if (conntemp) {
    // capture shared_ptr to keep object alive until invoked in loop thread
    loop_->queueInLoop([conntemp]() {
      LOG_DEBUG << "removeConnection queued for TcpConn ptr=" << conntemp.get()
                << " use_count=" << conntemp.use_count();
      conntemp->connectDestroyed();
    });
  }
  if (retry_.load(std::memory_order_acquire)) {
    LOG_INFO << "TcpClient::removeConnection - Reconnecting to "
             << serverAddr_.toIpPort();
    connector_->restart();
  }
}

void TcpClient::connection(int sockfd) {
  // set connetion name
  std::string name;
  Socket sock(sockfd);
  if (!sock.isvalid()) {
    LOG_ERROR << "TcpClient::connection - invalid socket";
    return;
  }
  std::variant<struct sockaddr_in, struct sockaddr_in6> localaddr =
      Socket::getLocalAddrVariant(sockfd);
  InetAddress localInet;
  std::visit(
      [&name, &localInet, this](auto &addr) {
        using T = std::decay_t<decltype(addr)>;
        if constexpr (std::is_same_v<T, struct sockaddr_in>) {
          name = name_ + " - " + std::to_string(ntohs(addr.sin_port));
          localInet = InetAddress(addr);
        } else if constexpr (std::is_same_v<T, struct sockaddr_in6>) {
          name = name_ + " - " + std::to_string(ntohs(addr.sin6_port));
          localInet = InetAddress(addr);
        }
      },
      localaddr);
  // if(std::holds_alternative<struct sockaddr_in>(localaddr))
  // {
  //     name = name_ + " - " + std::to_string(std::get<struct
  //                 sockaddr_in>(localaddr).sin_port);
  // }else{
  //     name = name_ + " - " + std::to_string(std::get<struct
  //                 sockaddr_in6>(localaddr).sin6_port);
  // }
  TcpConnPtr conn = std::make_shared<TcpConn>(loop_, name, std::move(sock),
                                              localInet, serverAddr_);
  if (messageCb_) {
    conn->setMessageCb(messageCb_.value());
  }
  if (connectionCb_) {
    conn->setConnectionCb(connectionCb_.value());
  }
  if (writeCompleteCb_) {
    conn->setWriteCompleteCb(writeCompleteCb_.value());
  }
  conn->setHandleCloseCb(
      [this](const TcpConnPtr &) { this->removeConnection(); });

  {
    std::unique_lock lock(shared_mutex_);
    tcpConnetion_ = conn;
  }

  // Use local shared_ptr to keep the connection alive across callback chain.
  conn->connectEstablished(); // 连接建立
  LOG_DEBUG << "TcpClient::connection - TcpConn established, name: "
            << conn->name()
            << ", localAddr: " << conn->localAddress().toIpPort()
            << ", peerAddr: " << conn->peerAddress().toIpPort();
}

} // namespace net
} // namespace mymoduo
