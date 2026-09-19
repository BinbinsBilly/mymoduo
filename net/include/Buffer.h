#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <type_traits>
#include <cassert>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <algorithm>

namespace mymoduo {
namespace net {

// 支持的整型类型判定（排除 bool）
// template<typename T>
// inline constexpr bool is_supported_int_type = std::is_integral_v<T> && !std::is_same_v<T,bool>;
template<typename T>    
    constexpr static bool is_supported_int_type = 
        std::is_same_v<T, int8_t>  || std::is_same_v<T, uint8_t>  ||
        std::is_same_v<T, int16_t> || std::is_same_v<T, uint16_t> ||
        std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t> ||
        std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t>;

class Buffer {
public:
    static const size_t kCheapPrepend = 8;          // 预留头部空间
    static constexpr char kCRLF[] = "\r\n";        // 行结束标记

    explicit Buffer(size_t initSize);                // 构造
    Buffer(const Buffer& buffer);                    // 拷贝构造

    //读取对端fd的数据 到 buffer的可写缓冲区
    ssize_t readFd(int fd, int* savederror);
    //将buffer的可读缓冲区的数据写到对端fd
    ssize_t writeFd(int fd, int* savederror);

    size_t readableBytes() const noexcept;           // 可读字节数
    size_t writeableBytes() const noexcept;          // 可写字节数
    size_t prependableBytes() const noexcept;        // 预留头部空间大小

    //缓冲区起始地址
    const char* bufferBegin() const noexcept;
    char* bufferBegin() noexcept;

    //可读数据的起始位置
    const char* peek() const noexcept;               // 读起始位置

    void retrieve(size_t len) noexcept;              // 取走 len 字节
    void retrieveUntil(const char* end) noexcept;    // 取走直到 end
    void retrieveAll() noexcept;                     // 重置

    template<typename T>
    auto retrieveT() noexcept -> std::enable_if_t<mymoduo::net::is_supported_int_type<T>, void>;
    template<typename T>
    auto readT() noexcept -> std::enable_if_t<mymoduo::net::is_supported_int_type<T>, T>;   // 读取整型（移动读指针）
    template<typename T>
    auto peekT() const noexcept -> std::enable_if_t<mymoduo::net::is_supported_int_type<T>, T>; // 窥视整型（不移动读指针）

    const char* findCRLF() const noexcept;           // 查找 CRLF
    const char* findCRLF(const char* start) const noexcept;
    const char* findEOL() const noexcept;            // 查找单个换行
    const char* findEOL(const char* start) const noexcept;

    std::string_view StringViewAll() const noexcept; // 所有数据视图
    std::string retrieveAllAsString();               // 取走全部并返回
    std::string retrieveAsString(size_t len);        // 取走 len 并返回

    //可写数据的起始位置
    const char* writeBegin() const noexcept;         // 写起始位置（const）
    char* writeBegin() noexcept;                     // 写起始位置

    //写相关
    void ensureWriteableBytes(size_t len);           // 确保可写空间
    void makeSpace(size_t len);                      // 扩容或搬移
    void hasWritten(size_t len) noexcept;            // 更新写指针

    //append会确保有足够空间后追加数据
    void append(const char* data, size_t len);       // 追加原始指针
    void append(const void* data, size_t len);       // 追加 void*
    void append(const std::string& str);             // 追加字符串
    template<typename T>
    auto appendT(T value) noexcept -> std::enable_if_t<mymoduo::net::is_supported_int_type<T>, void>; // 追加整型

    void prepend(const void* data, size_t len);      // 头部插入
    template<typename T>
    auto prependT(T value) noexcept -> std::enable_if_t<mymoduo::net::is_supported_int_type<T>, void>; // 头部插入整型
    
private:
    std::vector<char> buffer_;                       // 存储区
    size_t readerIndex_;                             // 读指针
    size_t writerIndex_;                             // 写指针
};

//以下整型读写模板定义在头文件中, 避免其他翻译单元实例化时产生 undefined symbol
template<typename T>
inline auto Buffer::retrieveT() noexcept -> std::enable_if_t<is_supported_int_type<T>, void>
{
    assert(readableBytes() >= sizeof(T));
    retrieve(sizeof(T));
}

template<typename T>
inline auto Buffer::readT() noexcept -> std::enable_if_t<is_supported_int_type<T>, T>
{
    T result = peekT<T>();
    retrieveT<T>();
    return result;
}

//后期需要修正字节序问题
template<typename T>
inline auto Buffer::peekT() const noexcept -> std::enable_if_t<is_supported_int_type<T>, T>
{
    assert(readableBytes() >= sizeof(T));
    T value{0};
    ::memcpy(&value, peek(), sizeof(T));
    return value;
}

} // namespace net
} // namespace mymoduo
