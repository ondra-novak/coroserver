#include "check.h"
#include <coroserver/stream.h>
#include <coroserver/character_io.h>
#include <coroserver/io_context.h>


cocls::async<void> write_task(coroserver::ContextIO ctx, std::string port) {
    coroserver::Stream stream = co_await ctx.connect(PeerName::lookup("localhost", port));
    coroserver::CharacterWriter<coroserver::Stream> wr(stream);
    for (int i = 0; i < 655360; i++) {
        co_await wr(static_cast<char>(i & 0xFF));
    }
    co_await wr.flush();
}

cocls::async<void> read_task(coroserver::Stream s) {
    int x = 0;
    int cnt = 0;
    coroserver::CharacterReader<coroserver::Stream> rd(s);
    int c = co_await rd;
    while (c != -1) {
        CHECK_EQUAL((x & 0xFF) , c);
        c = co_await rd;
        ++x;
        ++cnt;
    }
    CHECK_EQUAL(cnt, 655360);
}

cocls::async<void> server_task(cocls::future<coroserver::Stream> &f) {

    coroserver::Stream s = co_await f;
    co_await read_task(s);

}

int main() {

    coroserver::ContextIO ctx = coroserver::ContextIO::create();

    auto addr = PeerName::lookup("127.0.0.1","*");
    auto listener = ctx.accept(addr);

    cocls::future<coroserver::Stream> f([&]{return listener();});

    write_task(ctx, addr[0].get_port()).detach();

    server_task(f).join();


}
