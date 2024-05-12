#include <coroserver/context.h>

#include <iostream>
#include <coroserver/peername.h>
#include <coroserver/http_server_request.h>
#include <coroserver/http_server.h>

using namespace coroserver;

coro::async<void> co_main(Stream s) {
    http::ServerRequest req(s);
    while (co_await req.load()) {
        if (req.allow({http::Method::GET,http::Method::HEAD})) {
            std::string path(req.url_decode(req.get_path().substr(1)));
            req.content_type_from_extension(path);
            if (!co_await req.send_file(path)) {
                req.set_status(404);
                req.content_type(http::ContentType::text_plain_utf8);
                co_await req.send("Not found");
                std::cout << "Not found " << path << std::endl;
            } else {
                std::cout << "Served " << path << std::endl;
            }
        } else{
            co_await req.send("");
        }
        if (!req.keep_alive())
            break;
    }
}


int main() {

    auto addrs = PeerName::lookup(":10000","");
    Context ctx(1);
    auto fin = ctx.tcp_server([&](Stream &&s){
        co_main(std::move(s)).detach();
    },std::move(addrs));
    std::cout << "Press enter to stop server:" << std::endl;
    std::cin.get();
    ctx.stop();
    fin.wait();
    return 0;
}

