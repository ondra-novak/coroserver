#include <iostream>
#include <coroserver/https_client.h>



coro::async<void> make_GET_request(coroserver::https::Client &client) {
    coroserver::http::ClientRequest req(co_await client.open(coroserver::http::Method::GET, "https://eu.httpbin.org/get?aa=10"));
    coroserver::Stream response = co_await req.send();
    std::string data;
    co_await response.read_block(data, -1);
    std::cout << "  Status: " << req.get_status() << std::endl;
    std::cout << "  Status Message: " << req.get_status_message() << std::endl;
    for (const auto &x: req.headers()) {
        std::cout << "  " << x.first << ": " << x.second << std::endl;
    }
    std::cout << data<< std::endl;

}


coro::async<void> make_POST_request(coroserver::https::Client &client) {
    coroserver::http::ClientRequest req(co_await client.open(coroserver::http::Method::POST, "https://eu.httpbin.org/post?aa=10"));
    coroserver::Stream response = co_await req.send("Ahoj=Nazdar");
    std::string data;
    co_await response.read_block(data, -1);
    std::cout << "  Status: " << req.get_status() << std::endl;
    std::cout << "  Status Message: " << req.get_status_message() << std::endl;
    for (const auto &x: req.headers()) {
        std::cout << "  " << x.first << ": " << x.second << std::endl;
    }
    std::cout << data<< std::endl;

}

coro::async<void> make_POST_request_te(coroserver::https::Client &client) {
    coroserver::http::ClientRequest req(co_await client.open(coroserver::http::Method::POST, "https://eu.httpbin.org/post?aa=te"));
    coroserver::Stream body = co_await req.begin_body();
    co_await body.write("Ahoj nazdar");
    co_await body.write("...");
    co_await body.write("Next chunk");
    coroserver::Stream response = co_await req.send();
    std::string data;
    co_await response.read_block(data, -1);
    std::cout << "  Status: " << req.get_status() << std::endl;
    std::cout << "  Status Message: " << req.get_status_message() << std::endl;
    for (const auto &x: req.headers()) {
        std::cout << "  " << x.first << ": " << x.second << std::endl;
    }
    std::cout << data<< std::endl;

}

coro::async<void> make_POST_request_te_except(coroserver::https::Client &client, bool fail) {
    coroserver::http::ClientRequest req(co_await client.open(coroserver::http::Method::POST, fail?"https://eu.httpbin.org/status/403":"https://eu.httpbin.org/post?aa=te"));
    req.expect100continue();
    coroserver::Stream body = co_await req.begin_body();
    if (req.get_status() == 100) {
        std::cout << "Received 100 continue" << std::endl;
        co_await body.write("Ahoj nazdar");
        co_await body.write("...");
        co_await body.write("Next chunk");
    }
    coroserver::Stream response = co_await req.send();
    std::string data;
    co_await response.read_block(data, -1);
    std::cout << "  Status: " << req.get_status() << std::endl;
    std::cout << "  Status Message: " << req.get_status_message() << std::endl;
    for (const auto &x: req.headers()) {
        std::cout << "  " << x.first << ": " << x.second << std::endl;
    }
    std::cout << data<< std::endl;

}

coro::async<void> make_POST_request_te_except_empty(coroserver::https::Client &client) {
    coroserver::http::ClientRequest req(co_await client.open(coroserver::http::Method::POST, "https://eu.httpbin.org/post?aa=te"));
    req.expect100continue();
    coroserver::Stream response = co_await req.send();
    std::string data;
    co_await response.read_block(data, -1);
    std::cout << "  Status: " << req.get_status() << std::endl;
    std::cout << "  Status Message: " << req.get_status_message() << std::endl;
    for (const auto &x: req.headers()) {
        std::cout << "  " << x.first << ": " << x.second << std::endl;
    }
    std::cout << data<< std::endl;

}



int main() {

    coroserver::ssl::Context ssl = coroserver::ssl::Context::init_client();
    coroserver::ContextIO ctx = coroserver::ContextIO::create(2);
    coroserver::https::Client httpc(ctx, ssl, "coroserver/20");
    make_GET_request(httpc).join();
    make_POST_request(httpc).join();
    make_POST_request_te(httpc).join();
    make_POST_request_te_except(httpc, false).join();
    make_POST_request_te_except(httpc, true).join();
    make_POST_request_te_except_empty(httpc).join();


    return 0;
}
