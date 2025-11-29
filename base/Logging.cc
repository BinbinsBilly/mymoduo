/**
 * @brief 日志的最终文件 调用LogStream的接口 重载<<
 * version:1.0  !!!线程信息保留实现!!!
 */


#include "Logging.h"

#include <stdio.h>

void DefaultOutput(const char* msg, int len)
{
    size_t n = fwrite(msg, 1, len, stdout);
    (void)n; //避免未使用warning
}

namespace mymoduo
{
Logger::LogLevel initLogLevel()
{
    if(::getenv("MYMUDUO_LOG_DEBUG"))
    {
        return Logger::DEBUG;
    }
    else
    {
        return Logger::INFO;
    }
}
Logger::LogLevel g_LogLevel = initLogLevel();
void Logger::setLogLevel(Logger::LogLevel level)
{
    g_LogLevel = level;
}

template<int N>
Logger::SourceFile::SourceFile(const char (&arr)[N])
    : data_(arr),
        size_(N - 1)//除去'\0'
{
    if(const char* slash = strrchr(data_, '/')) //找到最后一个'/'
    {
        data_ = slash + 1; //指向文件名开始处
        size_ -= static_cast<size_t>(data_ - arr);
    }
}
Logger::SourceFile::SourceFile(const char* filename)
    : data_(filename)
{
    size_ = strlen(filename);
    const char* slash = strrchr(filename, '/');
    if(slash)
    {
        data_ = slash + 1;
        size_ -= static_cast<size_t>(data_ - filename);
    }
}

//辅助类
class T
{
    public:
        T(const char* str, unsigned len)
            :str_(str),
                len_(len)
        {}
    const char* str_;
    unsigned len_;
};

inline LogStream& operator<<(LogStream& s, const T v)
{
    s.append(v.str_, v.len_);
    return s;
}

inline LogStream& operator<<(LogStream& s, const Logger::SourceFile& v)
{
    s.append(v.data_, v.size_);
    return s;
}

Logger::Impl::Impl(LogLevel level, int old_errno, const SourceFile& basefile, int line)
    :
        time_(base::TimeStamp::now()),
        stream_(),
        level_(level),
        basename_(basefile),
        line_(line)
{
    (void)old_errno; //暂时忽略!!!!!
    formatTime();
    //后面还要包含线程信息
    //...
}
void Logger::Impl::formatTime()
{
    std::string formattime = time_.toFormatterString();
    stream_.append(formattime.c_str(), static_cast<int>(formattime.length()));
    stream_.append("\n", 1);
}   
void Logger::Impl::finish()
{
    stream_ << " - " << basename_ << ':' << line_ << '\n';
}

Logger::Logger(SourceFile file, int line)
    :impl_(INFO, 0, file, line),
        Logout_(DefaultOutput)
{}
Logger::Logger(SourceFile file, int line, LogLevel level)
    :impl_(level, 0, file, line),
        Logout_(DefaultOutput)
{}
Logger::Logger(SourceFile file, int line, LogLevel level, const char* func)
    :impl_(level, 0, file, line),
        Logout_(DefaultOutput)
{
    impl_.stream_<<func<<" ";
}

LogStream& Logger::stream()
{
    return impl_.stream_;
}

void Logger::setOutputFunc(OutputFunc outputFunc)
{
    //允许用户自定义输出函数；若传入为空则保持当前默认函数
    if(outputFunc)
    {
        Logout_ = std::move(outputFunc);
    }
}

Logger::~Logger()
{
    impl_.finish();
    const LogStream::Buffer &buf(stream().buffer());
    Logout_(buf.data(), buf.lenth());
}

} // namespace mymoduo



