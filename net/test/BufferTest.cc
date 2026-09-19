#include "Buffer.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <cstring>

using mymoduo::net::Buffer;

static void testAppendAndRetrieve() {
    Buffer buf(16);
    std::string s = "hello";
    buf.append(s);
    assert(buf.readableBytes() == s.size());
    std::string out = buf.retrieveAsString(3);
    assert(out == "hel");
    assert(buf.readableBytes() == 2);
    out = buf.retrieveAllAsString();
    assert(out == "lo");
    assert(buf.readableBytes() == 0);
}

static void testMakeSpaceAndGrowth() {
    Buffer buf(8); // internal total = kCheapPrepend + 8
    std::string a(10, 'a');
    buf.append(a); // force growth
    assert(buf.readableBytes() == 10);
    size_t oldCap = buf.writeableBytes();
    std::string b(20, 'b');
    buf.append(b); // should reallocate or move
    assert(buf.readableBytes() == 30);
    assert(buf.readableBytes() >= oldCap); // basic sanity
}

static void testPrepend() {
    Buffer buf(8);
    std::string payload = "world";
    buf.append(payload);
    int header = 1234;
    // 预留空间足够才能 prepend
    assert(buf.prependableBytes() >= sizeof(header));
    buf.prepend(&header, sizeof(header));
    // 可读部分包含 header + payload
    assert(buf.readableBytes() == sizeof(header) + payload.size());
    // 读取前四字节验证 header
    int readHeader = 0;
    ::memcpy(&readHeader, buf.peek(), sizeof(header));
    assert(readHeader == header);
}

static void testFindCRLFAndEOL() {
    Buffer buf(32);
    std::string line = "GET / HTTP/1.1\r\nHost: test\r\n";
    buf.append(line);
    const char* first = buf.findCRLF();
    assert(first != nullptr);
    assert(std::string(first, 2) == "\r\n");
    // 查找第二个 CRLF
    const char* second = buf.findCRLF(first + 2);
    assert(second != nullptr);
    // EOL 查找换行
    const char* eol = buf.findEOL();
    assert(eol != nullptr);
}

static void testStringViewAll() {
    Buffer buf(16);
    buf.append("abc", 3);
    auto sv = buf.StringViewAll();
    assert(sv.size() == 3);
    assert(sv == "abc");
}

static void testPeekT() {
    Buffer buf(16);
    uint32_t value = 0x12345678u;
    buf.append(&value, sizeof(value));
    // peekT 只窥视，不移动读指针
    assert(buf.peekT<uint32_t>() == value);
    assert(buf.readableBytes() == sizeof(value));
}

static void testReadTUint32() {
    Buffer buf(16);
    uint32_t value = 0xdeadbeefu;
    buf.append(&value, sizeof(value));
    // readT 返回正确值，且读指针前移 4 字节
    assert(buf.readT<uint32_t>() == value);
    assert(buf.readableBytes() == 0);
}

static void testReadTInt16() {
    Buffer buf(16);
    int16_t a = -1234;
    int16_t b = 4321;
    buf.append(&a, sizeof(a));
    buf.append(&b, sizeof(b));
    assert(buf.readableBytes() == 4);
    assert(buf.readT<int16_t>() == a);
    assert(buf.readableBytes() == 2);
    assert(buf.readT<int16_t>() == b);
    assert(buf.readableBytes() == 0);
}

void test()
{
    testAppendAndRetrieve();
    testMakeSpaceAndGrowth();
    testPrepend();
    testFindCRLFAndEOL();
    testStringViewAll();
    testPeekT();
    testReadTUint32();
    testReadTInt16();
    std::cout << "All Buffer tests passed.\n";
}

int main() {
    testAppendAndRetrieve();
    testMakeSpaceAndGrowth();
    testPrepend();
    testFindCRLFAndEOL();
    testStringViewAll();
    testPeekT();
    testReadTUint32();
    testReadTInt16();
    std::cout << "All Buffer tests passed.\n";
    return 0;
}