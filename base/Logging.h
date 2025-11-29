#pragma once

#include "noncopyable.h"
#include "LogStream.h"
#include "TimeStamp.h"

#include <string>
#include <string.h>
#include <functional>


//用户调用
#define LOG_DEBUG if(mymoduo::Logger::loglevel() <= mymoduo::Logger::DEBUG)\
    mymoduo::Logger(__FILE__, __LINE__, mymoduo::Logger::DEBUG, __func__).stream()
#define LOG_INFO if(mymoduo::Logger::loglevel() <= mymoduo::Logger::INFO)\
    mymoduo::Logger(__FILE__, __LINE__, mymoduo::Logger::INFO, __func__).stream()
#define LOG_WARN if(mymoduo::Logger::loglevel() <= mymoduo::Logger::WARN)\
    mymoduo::Logger(__FILE__, __LINE__, mymoduo::Logger::WARN, __func__).stream()
#define LOG_ERROR if(mymoduo::Logger::loglevel() <= mymoduo::Logger::ERROR)\
    mymoduo::Logger(__FILE__, __LINE__, mymoduo::Logger::ERROR, __func__).stream()
#define LOG_FATAL if(mymoduo::Logger::loglevel() <= mymoduo::Logger::FATAL)\
    mymoduo::Logger(__FILE__, __LINE__, mymoduo::Logger::FATAL, __func__).stream()


namespace mymoduo
{
    class Logger : noncopyable
    {
        public:
            enum LogLevel
            {
                DEBUG,
                INFO,
                WARN,
                ERROR,
                FATAL,
            };
            static LogLevel loglevel();  //日志级别:应是单例的
            static void setLogLevel(LogLevel level);

            class SourceFile
            {
                public:
                    //使用模板自动推导获取数组大小
                    //必须引用传入 防止退化为指针
                    template<int N>
                    SourceFile(const char (&arr)[N]);
                    SourceFile(const char* filename);
        
                    const char* data_;
                    size_t size_;
            };

            Logger(SourceFile file, int line);
            Logger(SourceFile file, int line, LogLevel level);
            Logger(SourceFile file, int line, LogLevel level, const char* func);
            ~Logger();

            LogStream& stream();
            using OutputFunc = std::function<void(const char* msg, int len)>;
            void setOutputFunc(OutputFunc);

        private:
            class Impl
            {
                public:
                    Impl(LogLevel level, int old_errno, const SourceFile& basefile, int line);

                    void finish();
                    static LogLevel level();

                    void formatTime();
                    base::TimeStamp time_;
                    LogStream stream_;
                    LogLevel level_;
                    SourceFile basename_;
                    int line_;
            };
            Impl impl_;
            OutputFunc Logout_;
    };
    extern Logger::LogLevel g_LogLevel;
    inline Logger::LogLevel Logger::loglevel()
    {
        return g_LogLevel;
    }
}

