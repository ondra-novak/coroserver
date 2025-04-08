#include <coroserver/client.hpp>
#include <coroserver/timer.hpp>
#include <coroserver/stream.hpp>
#include <basic_coro/when_all.hpp>

using namespace coroserver;

static coro::mutex console_lock;

coro::awaitable<void> async_write(Timer &tmr, Stream s) {
    while (co_await tmr.sleep_for(std::chrono::seconds(1))) {        
        auto own = co_await console_lock.lock();
        co_await s.write("... and next line\r\n");
    }
}

coro::awaitable<void> async_read(Stream s) {
    std::string ln;
    while (true) {
        co_await s.read_until(ln,"\n");
        if (ln.compare(0,4,"exit") == 0) break;
        ln.push_back('\n');
        auto own = co_await console_lock.lock();
        co_await s.write(ln);
    }

}


int main() {
    Context ctx = Context::create(1);
    Stream s= connect_stdinout(ctx);
    s.write("Type anything, type 'exit' to terminate program \n").wait();
    Timer tmr = Timer::create(ctx);
    auto aw = async_write(tmr, s);
    auto ar = async_read(s);
    auto waw = coro::when_all(aw);
    auto war = coro::when_all(ar);
    war.wait();
    tmr.shutdown();
    waw.wait();
}