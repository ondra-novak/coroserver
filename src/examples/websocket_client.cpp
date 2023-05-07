#include <coroserver/http_client.h>
#include <coroserver/http_ws_client.h>
#include <coroserver/signal.h>
#include <coroserver/pipe.h>
#include <cocls/queue.h>
#include <iostream>

using namespace coroserver;

using QueueMessage = std::variant<std::monostate, std::string>;

cocls::async<void> reader(ws::Stream stream, cocls::queue<QueueMessage> &q) {
    auto f = stream.read();
    while (co_await f.has_value()) {
        auto &msg = *f;
        if (msg.type == ws::Type::text) {
            std::cout << msg.payload << std::endl;
        }
        if (msg.type == ws::Type::connClose) {
            break;
        }
        f << [&]{return stream.read();};
    }
    std::cout << "Connection closed" << std::endl;
    q.push(std::monostate());
}

cocls::async<void> client(ContextIO ctx, cocls::queue<QueueMessage> &data) {

    http::Client httpc(ctx, "userver/20");
    http::ClientRequest req (co_await httpc.open(http::Method::GET, "http://127.0.0.1:10000/ws"));

        ws::Client wsclient({});
        cocls::future<ws::Stream> f = wsclient(req);
        if (co_await f.has_value()) {
            auto s = *f;
            auto t = reader(s,data).start();
            for(;;) {
                QueueMessage msg= std::move(co_await data.pop());
                if (std::holds_alternative<std::monostate>(msg)) break;
                co_await s.write(ws::Message{std::get<std::string>(msg), ws::Type::text});
            }
        } else {
            std::cerr << "Error to connect localhost:10000 /" << req.get_status() << std::endl;
        }

}

constexpr coroserver::search_kmp new_line("\n");
cocls::async<void> input(ContextIO ctx, cocls::queue<QueueMessage> &data) {
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

int main(int, char **) {

    ContextIO ctx = ContextIO::create(1);
    try {
        cocls::queue<QueueMessage> q;
        cocls::future<void> f = client(ctx,q).start();
        cocls::future<void> g = input(ctx,q).start();
        g.wait();
        std::cout << "Exiting..." << std::endl;
        ctx.stop();
        f.wait();
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
    }


    return 0;
}
