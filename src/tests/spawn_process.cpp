#include <coroserver/context.hpp>
#include <coroserver/client.hpp>
#include <coroserver/process.hpp>
#include "check.h"



coro::awaitable<int> run_child(coroserver::Context &ctx) {
    auto env = coroserver::Environment::current();
    std::string resp = env["RESPONSE"];
    coroserver::Stream s = coroserver::connect_stdinout(ctx);

    co_await s.write("welcome\n");
    co_await s.write(resp+"\n");

    std::string buffer;

    co_await s.read_block(buffer, 1000);
    std::reverse(buffer.begin(), buffer.end());
    co_await s.write(buffer);

    co_await s.close();
    co_return 10;
}


coro::awaitable<int> run_test(coroserver::Context &ctx, const char *arg0) {
    auto env = coroserver::Environment::current();
    std::string resp = "test_string";
    env["RESPONSE"] = resp ;
    coroserver::Stream s = coroserver::spawn_process(ctx, arg0, {"child"}, env);

    std::string buffer;

    co_await s.read_until(buffer, "\n", 1000);

    CHECK_EQUAL(buffer, "welcome");

    co_await s.read_until(buffer, "\n", 1000);

    CHECK_EQUAL(buffer, resp);

    co_await s.write("hello world!");
    co_await s.close();

    co_await s.read_block(buffer, 1000);
    CHECK_EQUAL(buffer, "!dlrow olleh");


    int r = co_await get_process_exit_status(s);
    CHECK_EQUAL(r, 10);

    co_return 0;
}





int main(int argc, char **argv) {

    auto ctx = coroserver::Context::create(1);

    if (argc == 2 && std::string_view(argv[1]) == "child") {

        return run_child(ctx);

    } else {

        return run_test(ctx, argv[0]);

    }
}
