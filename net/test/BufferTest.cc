#include "Buffer.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <unistd.h>

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

// readFd 回归测试：数据量超过 writable，触发 readv 双 iov（主缓冲区 + 65536 字节栈上 extrabuf）路径。
// 修复前 bug：先执行 writerIndex_ += writable 再计算 append 长度，导致 writeableBytes() 已为 0，
// append(extrabuf, n - 0) 从 65536 字节的栈数组中越界读取 n 字节（n 可达 writable+65536 > 65536），
// 既越界读栈又造成数据损坏/重复。ASan 可捕获越界，内容逐字节校验可捕获数据错误。
static void testReadFdExtrabufOverflow() {
    // 大数据量用例：100000 字节确定性模式
    const size_t fileSize = 100000;
    std::vector<char> pattern(fileSize);
    for (size_t i = 0; i < fileSize; ++i) {
        pattern[i] = static_cast<char>(i % 251);
    }

    FILE* fp = ::tmpfile();
    assert(fp != nullptr);
    assert(::fwrite(pattern.data(), 1, fileSize, fp) == fileSize);
    assert(::fflush(fp) == 0);
    const int fd = ::fileno(fp);
    assert(fd >= 0);
    assert(::lseek(fd, 0, SEEK_SET) == 0);

    Buffer buf(1024); // muduo 风格默认初始大小（无默认构造，显式传入）
    const size_t writable = buf.writeableBytes();
    assert(writable > 0);
    assert(writable < 65536); // 保证 readv 走双 iov 路径

    int savedErrno = 0;
    const ssize_t n = buf.readFd(fd, &savedErrno);
    // readv 一次最多读满 writable + sizeof(extrabuf)
    const ssize_t expected = static_cast<ssize_t>(std::min<size_t>(fileSize, writable + 65536));
    assert(n == expected);
    assert(n > 65536); // 确保确实走 extrabuf 路径（旧 bug 在此场景越界读栈）
    assert(buf.readableBytes() == static_cast<size_t>(n));

    std::string data = buf.retrieveAllAsString();
    assert(data.size() == static_cast<size_t>(n));
    // 逐字节校验内容：旧 bug 会因越界读取导致内容错误/重复
    for (size_t i = 0; i < data.size(); ++i) {
        assert(data[i] == pattern[i]);
    }
    assert(buf.readableBytes() == 0);
    ::fclose(fp);

    // 小数据量用例：数据量 < writable，走单 iov 路径，验证内容正确
    const size_t smallSize = 64;
    std::vector<char> small(smallSize);
    for (size_t i = 0; i < smallSize; ++i) {
        small[i] = static_cast<char>((i * 7 + 3) % 251);
    }
    FILE* fp2 = ::tmpfile();
    assert(fp2 != nullptr);
    assert(::fwrite(small.data(), 1, smallSize, fp2) == smallSize);
    assert(::fflush(fp2) == 0);
    const int fd2 = ::fileno(fp2);
    assert(fd2 >= 0);
    assert(::lseek(fd2, 0, SEEK_SET) == 0);

    Buffer buf2(1024);
    assert(buf2.writeableBytes() > smallSize);
    int savedErrno2 = 0;
    const ssize_t n2 = buf2.readFd(fd2, &savedErrno2);
    assert(n2 == static_cast<ssize_t>(smallSize));
    assert(buf2.readableBytes() == smallSize);
    std::string data2 = buf2.retrieveAllAsString();
    assert(data2.size() == smallSize);
    for (size_t i = 0; i < smallSize; ++i) {
        assert(data2[i] == small[i]);
    }
    ::fclose(fp2);
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
    testReadFdExtrabufOverflow();
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
    testReadFdExtrabufOverflow();
    std::cout << "All Buffer tests passed.\n";
    return 0;
}