#include <coroserver/http_client.h>
#include <coroserver/http_ws_client.h>
#include <coroserver/signal.h>
#include <coroserver/stream_utils.h>
#include <coro.h>
#include <iostream>
#include <csignal>

using namespace coroserver;

using QueueMessage = std::variant<std::monostate, std::string>;

coro::async<void> reader(ws::Stream stream, coro::queue<QueueMessage> &q) {
    auto f = stream.receive();
    while (co_await f.has_value()) {
        const auto &msg = f.get();
        if (msg.type == ws::Type::text) {
            std::cout << msg.payload << std::endl;
        }
        if (msg.type == ws::Type::connClose) {
            break;
        }
        f << [&]{return stream.receive();};
    }
    std::cout << "Connection closed" << std::endl;
    q.push(std::monostate());
}

coro::async<void> client(Context &ctx, coro::queue<QueueMessage> &data) {

    try {
        http::Client httpc(ctx, "userver/20");
        http::ClientRequest req (co_await httpc.open(http::Method::GET, "http://127.0.0.1:10000/ws"));

        try {
            ws::Stream s = co_await ws::Client::connect(req);
            auto t = reader(s,data).start();
            for(;;) {
                QueueMessage msg= std::move(co_await data.pop());
                if (std::holds_alternative<std::monostate>(msg)) {
                    s.close();
                    co_await t;
                    break;;
                }
                co_await s.send(ws::Message{std::get<std::string>(msg), ws::Type::text});
            }
        } catch (coro::broken_promise_exception &e) {
                std::cerr << "Error to connect localhost:10000 /" << req.get_status() << std::endl;
        }
    } catch (const std::exception &e) {
            std::cerr << "Failed to connect localhost:10000 /" << e.what() << std::endl;
    }
}

constexpr coroserver::kmp_pattern new_line("\n");

coro::async<void> input(Context &ctx, coro::queue<QueueMessage> &data) {
    Stream s = ctx.create_stdio();
    std::string line;
    std::string name = "client: ";
    co_await s.write("use /<name> to change name\n");
    ReadUntil rdline(s);
    while (true) {
        auto ln = co_await rdline(new_line, true);
        if (ln.empty()) break;
        ln = ln.substr(0, ln.size()-1);
        if (ln.empty()) continue;
        bool issw = ln[0] == '/';
        if (issw) {
            name = ln.substr(1);
            name.append(": ");
        } else {
            line = name;
            line.append(ln);
            data.push(line);
        }
    }
    data.push(std::monostate());
}


int main(int, char **) {


    Context ctx(0);
    try {
        coro::queue<QueueMessage> q;
        coro::future<void> f = client(ctx,q).start();
        coro::future<void> g = input(ctx,q).start();
        ctx.get_scheduler().await(f);
        std::cout << "Exiting..." << std::endl;
        ctx.stop();
        f.wait();
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}
