#include "noncopyable.h"
#include "Logging.h"
#include "LogStream.h"

#include <iostream>


void test01()
{
    LOG_INFO << "Log Test!";
}

int main()
{
    test01();
    return 0;
}
