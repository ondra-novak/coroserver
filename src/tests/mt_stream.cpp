#include "check.h"
#include <coroserver/mt_stream.h>

#include "test_stream.h"
coro::async<void> test_writer() {
    std::string out;
    auto stream = TestStream<100>::create({}, &out);
    coroserver::MTStreamWriter wr(stream);
    wr.write("hello");
    wr.write("test");
    wr.write("test2");
    wr.write("test3");
    auto tm = std::chrono::system_clock::now();
    co_await wr.write("");
    auto tm2 = std::chrono::system_clock::now();
    wr.write("test4");
    co_await wr.write_eof();
    CHECK_EQUAL(out, "hellotesttest2test3test4");
    auto tm3 = std::chrono::system_clock::now();
    int ms200 = std::chrono::duration_cast<std::chrono::milliseconds>(tm2-tm).count();
    int ms300 = std::chrono::duration_cast<std::chrono::milliseconds>(tm3-tm).count();
    CHECK_BETWEEN(190,ms200,230);
    CHECK_BETWEEN(290,ms300,330);



}

#include "test_stream.h"
void test_writer_2() {
    std::string out;
    auto stream = TestStream<100>::create({}, &out);
    coroserver::MTStreamWriter wr(stream);
    wr.write("hello");
    wr.write("test");
    wr.write("test2");
    wr.write("test3");
    auto tm = std::chrono::system_clock::now();
    wr.write("").wait();
    auto tm2 = std::chrono::system_clock::now();
    wr.write("test4");
    wr.write_eof().wait();
    CHECK_EQUAL(out, "hellotesttest2test3test4");
    auto tm3 = std::chrono::system_clock::now();
    int ms200 = std::chrono::duration_cast<std::chrono::milliseconds>(tm2-tm).count();
    int ms300 = std::chrono::duration_cast<std::chrono::milliseconds>(tm3-tm).count();
    CHECK_BETWEEN(190,ms200,210);
    CHECK_BETWEEN(290,ms300,310);



}

int main() {
    test_writer().run();
    test_writer_2();
return 0;
}
