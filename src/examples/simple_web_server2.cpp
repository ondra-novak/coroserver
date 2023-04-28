#include <coroserver/io_context.h>

#include <iostream>
#include <coroserver/peername.h>
#include <coroserver/http_server.h>
#include <coroserver/http_static_page.h>

using namespace coroserver;
#if 0
cocls::async<void> co_main(Stream s) {
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

#endif

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

