#pragma once

//不允许拷贝语义的类继承自noncopyable

namespace mymoduo
{
    class noncopyable
    {
        noncopyable(const noncopyable&) = delete;
        noncopyable& operator=(const noncopyable&) = delete;
    protected:
        noncopyable() = default;
        ~noncopyable() = default;
    };
}