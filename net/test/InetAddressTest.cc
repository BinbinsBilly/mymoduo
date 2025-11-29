#include "InetAddress.hpp"
#include <cassert>
#include <iostream>
#include <string>

using IPv4Address = mymoduo::net::InetAddress;
using IPv6Address = mymoduo::net::InetAddress;


//error
static void testIPv4Loopback() {
    IPv4Address addr(8080, true); // loopback
    char buf[64];
    addr.toIp(buf, 64);
    std::cout << "IPv4 Loopback: " << buf << std::endl;
    LOG_INFO << "IPv4 Loopback: " << buf;
    assert(std::string(buf) == std::string("127.0.0.1"));
    assert(addr.toPort() == 8080);
}

static void testIPv4Any() {
    IPv4Address addr(9090, false); // any
    // 0.0.0.0 表示任意地址
    char buf[INET_ADDRSTRLEN];
    assert(addr.toIp(buf, sizeof(buf)) == "0.0.0.0");
    assert(addr.toPort() == 9090);
}

static void testIPv4FromString() {
    char buf[INET_ADDRSTRLEN];
    IPv4Address addr("192.168.1.100", 5000);
    assert(addr.toIp(buf, sizeof(buf)) == "192.168.1.100");
    assert(addr.toPort() == 5000);
}

static void testIPv6Loopback() {
    IPv6Address addr(7070, true, true);
    // ::1 loopback IPv6
    char buf[INET6_ADDRSTRLEN];
    assert(addr.toIp(buf, sizeof(buf)) == "::1");
    assert(addr.toPort() == 7070);
}

static void testIPv6FromString() {
    IPv6Address addr("2001:db8::1", 6000, true);
    char buf[INET6_ADDRSTRLEN];
    assert(addr.toIp(buf, sizeof(buf)) == "2001:db8::1");
    assert(addr.toPort() == 6000);
}

static void testResolveIPv4() {
    IPv4Address addr; // port 0
    bool ok = addr.resolve("localhost", &addr, AF_INET);
    assert(ok);
    // localhost IPv4 通常解析成 127.0.0.1
    char buf[INET_ADDRSTRLEN];
    assert(addr.toIp(buf, sizeof(buf)) == "127.0.0.1");
}

static void testResolveIPv6() {
    IPv6Address addr; // port 0
    bool ok = addr.resolve("localhost", &addr, AF_INET6);
    // 部分系统可能关闭 IPv6, 允许 ok 为 false 时跳过断言 IP
    if (ok) {
        // localhost IPv6 通常解析成 ::1
        char buf[INET6_ADDRSTRLEN];
        assert(addr.toIp(buf, sizeof(buf)) == "::1");
    }
}

int main() {
    testIPv4Loopback();
    testIPv4Any();
    testIPv4FromString();
    testIPv6Loopback();
    testIPv6FromString();
    testResolveIPv4();
    testResolveIPv6();
    std::cout << "All InetAddress tests passed.\n";
    return 0;
}
