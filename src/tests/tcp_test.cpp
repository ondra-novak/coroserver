#include "check.h"

#include <coroserver/network.h>
#include <coroserver/stream.h>
#include <coroserver/socket_stream.h>
#include <format>

using namespace coroserver;


awaitable<void> write_task(AsyncContext ctx, std::string addr) {
    Stream stream = SocketStream::connect(ctx,addr);

    for (int i = 0; i < 65536; i++) {
        co_await stream.send(std::format("{}\n", i));
    }
    co_await stream.close();
    CHECK(stream.get_state() == StreamState::closing);
    auto r = co_await stream.receive();
    CHECK_EQUAL(r.size(),0);
    CHECK(stream.get_state() == StreamState::closed);
    co_return;

}

awaitable<void> write_task2(AsyncContext ctx, std::string addr) {
    Stream stream = SocketStream::connect(ctx,addr);

    co_await stream.send("");
    for (int i = 0; i < 65536; i++) {
        stream.send(std::format("{}\n", i));
    }
    co_await stream.close();
    CHECK(stream.get_state() == StreamState::closing);
    auto r = co_await stream.receive();
    CHECK_EQUAL(r.size(),0);
    CHECK(stream.get_state() == StreamState::closed);
    co_return;

}

awaitable<void> read_task(TCPServer &server) {
    Stream stream = co_await server.accept();
    std::string line;
    for (int i = 0; i < 65536; ++i) {
        bool r = co_await stream.receive_until(line, "\n", 1000);
        CHECK(r);
        int n = strtol(line.c_str(),nullptr,10);
        CHECK_EQUAL(n,i);
    }
    auto rest = co_await stream.receive();
    CHECK_EQUAL(rest.size(),0);
    CHECK(stream.get_state() == StreamState::closed);
    co_return;
}




int test1() {

    std::string addr = "localhost:12112";
    AsyncContext ctx = make_async_context();
    TCPServer server(ctx, addr);
    allof_set wait_all;
    auto rdtask = read_task(server);
    auto wrtask = write_task(ctx, addr);
    wait_all.add(rdtask);
    wait_all.add(wrtask);
    wait_all.wait();
    return 0;
}

int test2() {

    std::string addr = "localhost:12112";
    AsyncContext ctx = make_async_context();
    TCPServer server(ctx, addr);
    allof_set wait_all;
    auto rdtask = read_task(server);
    auto wrtask = write_task2(ctx, addr);
    wait_all.add(rdtask);
    wait_all.add(wrtask);
    wait_all.wait();
    return 0;
}


int main() {
    test1();
    test2();
}
