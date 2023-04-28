#include <coroserver/io_context.h>

#include <iostream>
#include <coroserver/peername.h>
#include <coroserver/http_server.h>
#include <coroserver/http_static_page.h>

using namespace coroserver;

int main() {

    auto addrs = PeerName::lookup(":10000","");
    ContextIO ctx = ContextIO::create(1);
    coroserver::http::Server server;
    server.set_handler("/", http::Method::GET, http::StaticPage("www"));
    auto fin = server.start(ctx.accept(std::move(addrs)),http::DefaultLogger([](std::string_view line){
        std::cout << line << std::endl;
    }));
    std::cout << "Press enter to stop server:" << std::endl;
    std::cin.get();
    ctx.stop();
    fin.join();
    return 0;
}

