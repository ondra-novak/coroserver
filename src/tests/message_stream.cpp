#include "test_stream.h"
#include "check.h"
#include <coroserver/message_stream.h>


cocls::async<void> test(int size) {
    std::string in;
    std::string tmp;
    std::string out;
    for (int i = 0; i < size; i++) in.push_back((i & 0x3F) + 32);

    auto stream = TestStream<100>::create({}, &tmp);
    auto msgs = coroserver::MessageStream::create(stream);
    co_await msgs.write(in);
    co_await msgs.write_eof();

    stream = TestStream<100>::create({tmp});
    msgs = coroserver::MessageStream::create(stream);
    out = co_await msgs.read();
    auto tmp2 = co_await msgs.read();

    CHECK_EQUAL(tmp2, std::string_view());
    CHECK_EQUAL(out, in);


}

int main() {
    test(10).join();
    test(120).join();
    test(1024).join();
    test(65536).join();
    test(1645782).join();
}
