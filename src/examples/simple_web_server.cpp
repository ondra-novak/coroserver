#include <coroserver/context.hpp>
#include <coroserver/http_server_request.hpp>
#include <coroserver/server.hpp>


#include <iostream>

using namespace coroserver;

awaitable<void> co_main(Stream s) {
    http::ServerRequest req(s);
    while (co_await req.parse()) {
        auto m = req.filter_methods({http::Method::GET});
        if (m == http::Method::unknown) {
            co_await req.send_error(404);
            continue;
        }
        auto p = http::map_uri_to_path(std::filesystem::current_path(), req.get_path());
        if (!p.has_value()) {
            co_await req.send_error(404);
            continue;
        }
        if (std::filesystem::is_directory(*p)) {
            if (req.redirect_to_directory()) {
                co_await req.send("");
                continue;
            } else {
                *p = *p/"index.html";
            }
        }
        bool r = co_await req.send_file(*p);
        if (!r) {
            if (req.get_state() != http::ServerRequest::headers_sent) {
                std::cout << "Not found " << *p << std::endl;
                req.set_status(404);
                co_await req.send_error();
                continue;
            } else {
                break;
            }
        }
        std::cout << "Served " << *p << std::endl;
    }
}

awaitable<void> server(Context &ctx, std::stop_token tkn){    
    TCPServer server = TCPServer::create(ctx, "127.0.0.1", "10000");
    std::cout << "Server started on: " << server.get_host() << std::endl;
    std::stop_callback cb(tkn, [&](){
        server.shutdown();
    });

    while (true) {
        Stream s = co_await server.accept();
        co_main(s); //detached mode;
    }
}

std::optional<std::filesystem::path> find_www_directory(const std::filesystem::path& start_path) {
    auto current_path = start_path;
    while (!current_path.empty()) {
        auto candidate = current_path / "www";
        if (std::filesystem::exists(candidate) && std::filesystem::is_directory(candidate)) {
            return candidate;
        }
        current_path = current_path.parent_path();
    }
    return std::nullopt;
}

int main() {
    try {
        std::cout << "Starting server..." << std::endl;

    auto www_path = find_www_directory(std::filesystem::current_path());

    if (!www_path) {
        std::cerr << "No www directory found" << std::endl;
        return 1;
    }

    std::cout << "Using www directory: " << *www_path << std::endl;
    std::filesystem::current_path(*www_path);


    Context ctx = Context::create(1);
    std::stop_source ssrc;

    ctx.wait_for_exit_signal().set_callback([&](auto &){
        std::cout << "Break detected, stopping server" << std::endl;
        ssrc.request_stop();
    });

    server(ctx, ssrc.get_token()).await();

} catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
}

}

