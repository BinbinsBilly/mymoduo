#include "LogStream.h"

#include <algorithm>

namespace mymoduo
{
using Buffer = FixedBuffer<kSmallBuffer>;
namespace detail
{
//convert 数字->字符的算法实现
const char digits[] = "9876543210123456789";
const char* zero = digits + 9;

//传入buffer_的当前地址 和 要转换的整数
template <typename T>//要用模板类 因为要处理不同数据类型
size_t convert(char *buf, T value)
{
    T i = value;
    char* p = buf;
    do
    {
        int ldigit = static_cast<int>(i % 10);;
        i /= 10;
        *p++ = zero[ldigit];
    } while (i != 0);
    
    if(value < 0)
    {
        *p++ = '-';
    }
    *p = '\0';
    std::reverse(buf, p);  //反转[buf, p)
    return p - buf;         //返回转换后字符串长度
}

}
template class FixedBuffer<kSmallBuffer>;

template<typename T>
void LogStream::formatInteger(T v)
{
    if(buffer_.avail() >= kMaxNumericSize)
    {
        size_t len = detail::convert(buffer_.cur(), v);
        buffer_.add(len);
    }
}

LogStream& LogStream::operator<<(bool v)
{
    buffer_.append(v ? "1" : "0", 1);
    return *this;
}
//所有整型类型最终都转为int int来处理
LogStream& LogStream::operator<<(short v)
{
    *this << static_cast<int>(v);
    return *this;
}
LogStream& LogStream::operator<<(unsigned short v)
{
    *this << static_cast<unsigned int>(v);
    return *this;
}
LogStream& LogStream::operator<<(int v)
{
    formatInteger(v);
    return *this;
}
LogStream& LogStream::operator<<(unsigned int v)
{
    formatInteger(v);
    return *this;
};
LogStream& LogStream::operator<<(long v)
{
    formatInteger(v);
    return *this;
}
LogStream& LogStream::operator<<(unsigned long v)
{
    formatInteger(v);
    return *this;
}
LogStream& LogStream::operator<<(long long v)
{
    formatInteger(v);
    return *this;
}
LogStream& LogStream::operator<<(unsigned long long v)
{
    formatInteger(v);
    return *this;
}

LogStream& LogStream::operator<<(double v)
{
    if(buffer_.avail() >= kMaxNumericSize)
    {
        int len = snprintf(buffer_.cur(), kMaxNumericSize, "%.12g", v);
        buffer_.add(len);
    }
    return *this;
}

LogStream& LogStream::operator<<(char v)
{
    buffer_.append(&v, 1);
    return *this;
}
LogStream& LogStream::operator<<(const char* v)
{
    if(v)
    {
        buffer_.append(v, strlen(v));
    }
    else
    {
        buffer_.append("(NULL)", 6);
    }
    return *this;
}
LogStream& LogStream::operator<<(const std::string& v)
{
    if(!v.empty())
    {
        buffer_.append(v.c_str(), v.size());
    }
    else
    {
        buffer_.append("(NULL)", 6);
    }
    return *this;
}
const Buffer& LogStream::buffer() const
{
    return buffer_;
}
void LogStream::append(const char* data, int len)
{
    buffer_.append(data, len);
}
}