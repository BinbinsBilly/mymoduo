#include "Buffer.h"
#include "Logging.h"

#include <sys/uio.h>
#include <unistd.h>
#include <cstring>
#include <type_traits>

using namespace mymoduo;
using namespace mymoduo::net;

Buffer::Buffer(size_t initSize)
    :buffer_(kCheapPrepend + initSize),
     readerIndex_(kCheapPrepend),
     writerIndex_(kCheapPrepend)
{}

Buffer::Buffer(const Buffer& buffer)
    :buffer_(buffer.buffer_),
        readerIndex_(buffer.readerIndex_),
        writerIndex_(buffer.writerIndex_)
    {}

ssize_t Buffer::readFd(int fd, int* savederror)
{
    //空间不够的时候保留数据
    char extrabuf[65536];
    struct iovec vec[2];
    //必须先保存 writable: 后续 writerIndex_ 的修改会改变 writeableBytes() 的值,
    //曾因 append 长度误用修改后的值(n - 0 == n)导致从 extrabuf 栈越界读 + 数据损坏
    const size_t writable = writeableBytes();
    vec[0].iov_base = writeBegin();
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);
    const int iovec = (writable < sizeof(extrabuf)) ? 2 : 1;
    // readv 会先填满主缓冲区，剩余数据读到额外缓冲区
    const ssize_t n = ::readv(fd, vec, iovec);
    if(n < 0)
    {
        *savederror = errno;
    }else if (static_cast<size_t>(n) <= writable)
    {
        writerIndex_ += n;
        LOG_DEBUG << "[enough]Buffer::readFd - read " << n << " bytes from fd " << fd;
    }else{
        writerIndex_  += writable; //先写满原有缓冲区
        append(extrabuf, n - writable); //再写入额外缓冲区的数据
        LOG_DEBUG << "[not enough]Buffer::readFd - read " << n << " bytes from fd " << fd;
    }
    return n;
}

ssize_t Buffer::writeFd(int fd, int* savederror)
{
    ssize_t n = ::write(fd, peek(), readableBytes());
    if(n < 0)
    {
        LOG_ERROR << "Buffer::writeFd - write error: " << strerror(errno);
        *savederror = errno;
    }else if(n == 0)
    {
        LOG_WARN << "Buffer::writeFd - write 0 bytes to fd " << fd;
    }else{
        LOG_DEBUG << "Buffer::writeFd - write " << n << " bytes to fd " << fd;
    }
    return n;
}

size_t Buffer::readableBytes() const noexcept
{
    return writerIndex_ - readerIndex_;
}

size_t Buffer::writeableBytes() const noexcept
{
    return buffer_.size() - writerIndex_;
}

//空悬的空间
size_t Buffer::prependableBytes() const noexcept
{
    return readerIndex_;
}

const char* Buffer::bufferBegin() const noexcept
{
    return &*buffer_.begin();
}
char* Buffer::bufferBegin() noexcept
{
    return &*buffer_.begin();
}

//返回读起始地址
const char* Buffer::peek() const noexcept
{
    return bufferBegin() + readerIndex_;
}

void Buffer::retrieve(size_t len) noexcept
{
    if(len < readableBytes())
    {
        readerIndex_ += len;
    }
    else
    {
        retrieveAll();
    }
}

void Buffer::retrieveUntil(const char* end) noexcept
{
    retrieve(static_cast<size_t>(end - peek()));
}

void Buffer::retrieveAll() noexcept
{
    readerIndex_ = kCheapPrepend;
    writerIndex_ = kCheapPrepend;
}

const char* Buffer::findCRLF() const noexcept
{
    const char* crlf = std::search(peek(), writeBegin(),
                                   kCRLF, kCRLF + 2);
    return crlf == writeBegin() ? nullptr : crlf;
}
const char* Buffer::findCRLF(const char* start) const noexcept
{
    const char* crlf = std::search(start, writeBegin(),
                                   kCRLF, kCRLF + 2);
    return crlf == writeBegin() ? nullptr : crlf;
}

const char* Buffer::findEOL() const noexcept
{
    const void* eol = ::memchr(peek(), '\n', readableBytes());
    return static_cast<const char*>(eol);
}

const char* Buffer::findEOL(const char* start) const noexcept
{
    const void* eol = ::memchr(start, '\n', writeBegin() - start);
    return static_cast<const char*>(eol);
}

std::string_view Buffer::StringViewAll() const noexcept
{
    return std::string_view(peek(), readableBytes());
}

std::string Buffer::retrieveAllAsString()
{
    return retrieveAsString(readableBytes());
}

std::string Buffer::retrieveAsString(size_t len)
{
    len = std::min(len, readableBytes());
    std::string result(peek(), len);
    retrieve(len);
    return result;
}

const char* Buffer::writeBegin() const noexcept
{
    return bufferBegin() + writerIndex_;
}

char* Buffer::writeBegin() noexcept
{
    return bufferBegin() + writerIndex_;
}


void Buffer::ensureWriteableBytes(size_t len)
{
    if(writeableBytes() < len)
    {
        makeSpace(len);
    }
}

void Buffer::makeSpace(size_t len)
{
    //情况1:空间真的不够用
    if(writeableBytes() + prependableBytes() - kCheapPrepend < len)
    {
        LOG_INFO << "Buffer::makeSpace - need expand buffer from "
                 << buffer_.size() << " to " << (writerIndex_ + len);
        buffer_.resize(writerIndex_ + len);
    }
    else //情况2:空间够用 但是需要搬移数据
    {
        LOG_INFO << "Buffer::makeSpace - need move data from "
                 << "readerIndex_=" << readerIndex_
                 << ", writerIndex_=" << writerIndex_;
        size_t reaable = readableBytes();
        std::copy(bufferBegin() + readerIndex_, 
                  bufferBegin() + writerIndex_,
                  bufferBegin() + kCheapPrepend);
        readerIndex_ = kCheapPrepend;
        writerIndex_ = readerIndex_ + reaable;
        assert(reaable == readableBytes());
    }
}

void Buffer::hasWritten(size_t len) noexcept
{
    writerIndex_ += len;
}

void Buffer::append(const char* data, size_t len)
{
    ensureWriteableBytes(len);
    std::copy(data, data + len, writeBegin());
    hasWritten(len);
}
void Buffer::append(const void*data, size_t len)
{
    append(static_cast<const char*>(data), len);
}
void Buffer::append(const std::string& str)
{
    append(str.data(), str.size());
}
template<typename T>
auto Buffer::appendT(T value) noexcept -> std::enable_if_t<is_supported_int_type<T>, void>
{
    T be_value = value; //后期处理字节序问题
    append(&be_value, sizeof(be_value));
}

void Buffer::prepend(const void* data, size_t len)
{
    assert(len <= prependableBytes());
    readerIndex_ -= len;
    const char* msg = static_cast<const char*>(data);
    std::copy(msg, msg + len, bufferBegin() + readerIndex_);
}

template<typename T>
auto  Buffer::prependT(T value) noexcept -> std::enable_if_t<is_supported_int_type<T>, void>
{
    T msg = value; //后期要经过大端小端处理
    prepend(&msg, sizeof(T));
}