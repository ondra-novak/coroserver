#include "check.h"
#include "test_stream.h"
#include <coroserver/chunked_stream.h>


void test1() {
    std::string result;
    std::string expected = "6\r\nabc123\r\n1\r\nx\r\n44\r\nqwpoqiowpejxoiqwjdsqoiweqohsxioquhwdiuqhciwuegcyiuwbcuwyegdqwdqowdsq"
            "\r\n1a\r\nxxa23209jjew9j21232323232d\r\n0\r\n\r\n";

    auto s = TestStream<100>::create({}, &result);
    auto chs = coroserver::ChunkedStream::write(s);
    chs.write("abc123").wait();
    chs.write("x").wait();;
    chs.write("qwpoqiowpejxoiqwjdsqoiweqohsxioquhwdiuqhciwuegcyiuwbcuwyegdqwdqowdsq").wait();;
    chs.write("xxa23209jjew9j21232323232d").wait();;
    chs.write_eof().wait();;
    CHECK_EQUAL(result, expected);
}


void test2() {
    std::string expected="Hello world A long long string, long string, very long string x";
    auto s = TestStream<100>::create({
        "6\r\nHello \r\n",
        "6\r",
        "\nworld",
        " \r\n32\r\nA long long string, long string, very long string ",
        "\r\n1\r\nx\r\n0\r\n\r\nExtraData"
    });
    auto chs = coroserver::ChunkedStream::read(s);
    std::string result;
    {
        std::string_view buff = chs.read();
        while (!buff.empty()) {
            result.append(buff);
            buff = chs.read();
        }
    }
    CHECK_EQUAL(result, expected);
    std::string_view extra = s.read();
    CHECK_EQUAL(extra, "ExtraData");
}

int main() {
    test1();
    test2();





}
