#include "../coroserver/context.hpp"
#include <iostream>


using namespace coroserver ;

coro::awaitable<void> wait_coro(Context &ctx) {

    std::cout << "Generate break (Ctrl+C etc)" <<  std::endl;
    auto r = co_await ctx.wait_for_exit_signal();
    std::cout << "Break detected: " << static_cast<int>(r) << std::endl;

}

int main(int argc, char **argv) {

    bool wt = (argc == 2 && std::string_view(argv[1]) == "w");

    if (!wt) std::cout << "add 'w' argument to test exit timeout\n";

    Context ctx = Context::create(1);
    wait_coro(ctx).await();
    if (wt) std::this_thread::sleep_for(std::chrono::seconds(1000));

    return 0;
}
