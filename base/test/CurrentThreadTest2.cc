#include "CurrentThread.h"
#include <iostream>
#include <thread>
#include <cassert>

using std::cout;
using std::endl;


using namespace mymoduo::CurrentThread;

int main()
{
    cout << "current thread test2"<<endl;
    std::thread t([]{
        cout << "[thread]: "<< "Current thread id: " << threadId() << " Main thread id: " << detail::getMainThreadId() << endl;
        assert(!isMainThread());
    });
    t.join();
    cout << "[main]: "<< "Current thread id: " << threadId() << " Main thread id: " << detail::getMainThreadId() << endl;
    assert(isMainThread());
    return 0;
}