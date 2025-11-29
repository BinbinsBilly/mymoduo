#pragma once

#include "noncopyable.h"

#include <string>
#include <string.h>

namespace mymoduo
{
    const int kSmallBuffer = 4000;      //4KB

    template <int SIZE>
    class FixedBuffer : noncopyable
    {
        public:
        FixedBuffer()
            : cur_(data_)
        {}
        ~FixedBuffer() = default;

        //追加数据到缓冲区
        void append(const char* buf, size_t len)
        {
            if(static_cast<size_t>(avail()) >= len)
            {
                ::memcpy(cur_, buf, len);
                add(len);
            }    
        }

        //缓冲区可用空间
        int avail() const
        {
            return static_cast<size_t>(end() - cur_);
        }

        //更新缓冲区当前地址
        void add(size_t len)
        {
            cur_ += len;
        }

        //缓冲区结束地址
        const char* end() const
        {
            return data_ + sizeof(data_);
        }

        //返回缓冲区起始地址
        const char* data() const
        {
            return data_;
        }

        char* cur()
        {
            return cur_;
        }

        //将缓冲区内容转换为字符串
        std::string toString() const
        {
            return std::string(data_, lenth());
        }

        //缓冲区所用长度
        size_t lenth() const
        {
            return static_cast<size_t>(cur_ - data_);
        }
        
        private:
            char data_[SIZE];   //logger缓冲区
            char* cur_;        //当前地址
    };



    class LogStream : noncopyable
    {
        public:
        using Buffer = FixedBuffer<kSmallBuffer>;
            LogStream() = default;
            ~LogStream() = default;
            template<typename T>
            void formatInteger(T v);
            LogStream& operator<<(bool v);
            //数字类型要转化为字符字符类型
            LogStream& operator<<(short);
            LogStream& operator<<(unsigned short);
            LogStream& operator<<(int);
            LogStream& operator<<(unsigned int);
            LogStream& operator<<(long);
            LogStream& operator<<(unsigned long);
            LogStream& operator<<(long long);
            LogStream& operator<<(unsigned long long);
            LogStream& operator<<(double v);
            //字符类型
            LogStream& operator<<(char);
            LogStream& operator<<(const char*);
            LogStream& operator<<(const std::string&);

            const Buffer& buffer() const;
            void append(const char* data, int len);
            
        private:
            Buffer buffer_;
            static const int kMaxNumericSize = 48;  //数字最大长度 用于超限判断
    };



}