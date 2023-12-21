#include <coroserver/context.h>

#include <iostream>
#include <coroserver/peername.h>
#include <coroserver/http_server.h>
#include <coroserver/http_static_page.h>

using namespace coroserver;


int main() {

    auto addrs = PeerName::lookup(":10000","");
    Context ctx(0);
    coroserver::http::Server server;
    server.set_handler("/", http::Method::GET, http::StaticPage("www"));
    auto fin = server.start(ctx.accept(std::move(addrs)),http::DefaultLogger([](std::string_view line){
        std::cout << line << std::endl;
    }));
    std::cout << "Press CTRL+C to stop:" << std::endl;
    ctx.await(ctx.wait_for_intr());
    std::cout << "Exit" << std::endl;
    ctx.stop();
    fin.wait();
    return 0;
}

