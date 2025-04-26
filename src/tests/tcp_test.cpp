#include "check.h"

#include "../coroserver/stream.hpp"
#include "../coroserver/context.hpp"
#include "../coroserver/server.hpp"
#include "../coroserver/client.hpp"
#include <format>

using namespace coroserver;


coro::awaitable<void> write_task(Context ctx, std::string addr) {
    Stream stream = connect(ctx,addr,{});

    for (int i = 0; i < 65536; i++) {
        co_await stream.write(std::format("{}\n", i));
    }
    co_await stream.close();
    CHECK(stream.get_state() == StreamState::closing);
    auto r = co_await stream.read();
    CHECK_EQUAL(r.size(),0);
    CHECK(stream.get_state() == StreamState::closed);
    co_return;

}

coro::awaitable<void> write_task2(Context ctx, std::string addr) {
    Stream stream = connect(ctx,addr,{});

    std::ostringstream b;
    for (int i = 0; i < 65536; i++) {
        b << i << "\n";
    }

    co_await stream.write(b.view());
    co_await stream.close();
    CHECK(stream.get_state() == StreamState::closing);
    auto r = co_await stream.read();
    CHECK_EQUAL(r.size(),0);
    CHECK(stream.get_state() == StreamState::closed);
    co_return;

}

coro::awaitable<void> read_task(TCPServer &server) {
    Stream stream = co_await server.accept();
    std::string line;
    for (int i = 0; i < 65536; ++i) {
        bool r = co_await stream.read_until(line, "\n", 1000);
        CHECK(r);
        int n = strtol(line.c_str(),nullptr,10);
        CHECK_EQUAL(n,i);
    }
    std::string_view rest;
    rest = co_await stream.read();
    CHECK_EQUAL(rest.size(),0);
    CHECK(stream.get_state() == StreamState::closed);
    co_return;
}




int test1() {

    Context ctx = Context::create(1);
    TCPServer server = TCPServer::create(ctx, {}, {});
    auto rdtask = read_task(server);
    auto wrtask = write_task(ctx, server.get_host());
    coro::when_all(rdtask, wrtask);
    return 0;
}

int test2() {

    Context ctx = Context::create(1);
    TCPServer server = TCPServer::create(ctx, {}, {});
    auto rdtask = read_task(server);
    auto wrtask = write_task2(ctx, server.get_host());
    coro::when_all(rdtask, wrtask);
    return 0;
}


int main() {
    try {
        test1();
        test2();
    } catch (std::exception &e) {
        std::cerr << "EXCEPTION: " << e.what() << std::endl;
        return 1;
    }
}
