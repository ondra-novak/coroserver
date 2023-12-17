#include <coroserver/http_client.h>
#include <coroserver/http_ws_client.h>
#include <coroserver/signal.h>
//#include <coroserver/pipe.h>
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

coro::async<void> client(ContextIO ctx, coro::queue<QueueMessage> &data) {

    http::Client httpc(ctx, "userver/20");
    http::ClientRequest req (co_await httpc.open(http::Method::GET, "http://127.0.0.1:10000/ws"));

    try {
        ws::Stream s = co_await ws::Client::connect(req);
        auto t = reader(s,data).start();
        for(;;) {
            QueueMessage msg= std::move(co_await data.pop());
            if (std::holds_alternative<std::monostate>(msg)) break;
            co_await s.send(ws::Message{std::get<std::string>(msg), ws::Type::text});
        }
    } catch (coro::broken_promise_exception &e) {
            std::cerr << "Error to connect localhost:10000 /" << req.get_status() << std::endl;
    }
}

constexpr coroserver::search_kmp new_line("\n");
coro::async<void> input(ContextIO ctx, coro::queue<QueueMessage> &data) {
    Stream s = PipeStream::stdio(ctx);
    std::string line;
    std::string name = "client: ";
    co_await s.write("use /<name> to change name\n");
    auto ru = s.read_until(line, new_line);
    while (co_await ru()) {
        bool issw = line[0] == '/';
        line.pop_back();
        if (issw) {
            name = line.substr(1)+": ";
        } else {
            data.push(name+line);
        }
        line.clear();
    }
}

coro::async<void> signal_stop(ContextIO ctx) {
    try {
        SignalHandler sighandler(ctx);
        auto f =  sighandler({SIGINT, SIGTERM});
        co_await f;
        co_await ctx.stop(); //here all blocking operations are canceled
    } catch (...) {
    }
}
int main(int, char **) {


    ContextIO ctx = ContextIO::create(1);
    try {
        coro::queue<QueueMessage> q;
        coro::future<void> f = client(ctx,q).start();
        coro::future<void> g = input(ctx,q).start();
        coro::future<void> h = signal_stop(ctx);
        g.wait();
        std::cout << "Exiting..." << std::endl;
        ctx.stop();
        f.wait();
        h.wait();
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}
