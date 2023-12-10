#include "check.h"
#include "test_stream.h"

#include <coroserver/character_io.h>

#include <memory>
coro::async<void> test_read() {
    std::string expected="The quick brown fox jumps over the lazy dog";
    std::vector<std::string> stream = {"The quick brown"," fox jumps over"," the lazy do","g"};
    coroserver::Stream s (std::make_shared<TestStream<100> >(std::move(stream)));

    coroserver::CharacterReader rd(s);
    for (char c: expected) {
        int i = co_await rd;
        CHECK_EQUAL(i, c);
    }
    int i = co_await rd;
    CHECK_EQUAL(i,-1);

}

coro::async<void> test_write() {
    std::string expected="The quick brown fox jumps over the lazy dog";
    std::string output;
    coroserver::Stream s (std::make_shared<TestStream<100> >(std::vector<std::string>(), &output));

    coroserver::CharacterWriter<coroserver::Stream, 10> wr(s);
    for (char c: expected) {
        co_await wr(c);
    }
    co_await wr.flush();
    CHECK_EQUAL(output, expected);

}

int main() {
    test_read().join();
    test_write().join();

}
