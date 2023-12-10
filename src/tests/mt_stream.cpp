#include "check.h"
#include <coroserver/mt_stream.h>

#include "test_stream.h"
coro::async<void> test_writer() {
    std::string out;
    auto stream = TestStream<100>::create({}, &out);
    coroserver::MTStreamWriter wr(stream);
    wr("hello");
    wr("test");
    wr("test2");
    wr("test3");
    auto tm = std::chrono::system_clock::now();
    co_await wr.wait_for_flush();
    auto tm2 = std::chrono::system_clock::now();
    wr("test4");
    co_await wr.wait_for_idle();
    auto tm3 = std::chrono::system_clock::now();
    int ms100 = std::chrono::duration_cast<std::chrono::milliseconds>(tm2-tm).count();
    int ms300 = std::chrono::duration_cast<std::chrono::milliseconds>(tm3-tm).count();
    CHECK_BETWEEN(90,ms100,110);
    CHECK_BETWEEN(290,ms300,310);
    CHECK_EQUAL(out, "hellotesttest2test3test4");



}

int main() {
    test_writer().join();
return 0;
}

