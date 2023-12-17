#include "check.h"
#include "test_stream.h"
#include <coroserver/http_server_request.h>
#include <coroserver/http_server.h>
#include <coroserver/stream_utils.h>


using namespace coroserver;
using namespace coroserver::http;



coro::async<void> test_GET_http10() {

    std::string out;
    auto s = TestStream<50>::create({"GET /path HTTP/1.0\r\nHost: example.com\r\nX-Header: ","test\r\nX-Header2 : test2\r\n","\r\n"}, &out);

    ServerRequest req(s);
    bool loaded = co_await req.load();
    CHECK(loaded);
    CHECK_EQUAL(req.get_host(),"example.com");
    CHECK_EQUAL(req.get_path(),"/path");
    CHECK(req.get_method() == Method::GET);
    CHECK(req.keep_alive()== false);
    CHECK(req.get_version() == Version::http1_0);
    req.add_date(std::chrono::system_clock::from_time_t(1651236587));
    req.content_type(ContentType::text_html_utf8);
    co_await req.send("<html><body>It's works</body></html>");
    CHECK(out == "HTTP/1.0 200 OK\r\nDate: Fri, 29 Apr 2022 12:49:47 GMT\r\nContent-Type: text/html;charset=utf-8\r\nContent-Length: 36\r\nServer: CoroServer 1.0 (C++20)\r\nConnection: close\r\n\r\n<html><body>It's works</body></html>");
}
coro::async<void> test_GET_http10_infstrm() {

    std::string out;
    auto s = TestStream<50>::create({"GET /path HTTP/1.0\r\nHost: example.com\r\nX-Header: ","test\r\nX-Header2 : test2\r\n","\r\n"}, &out);

    ServerRequest req(s);
    bool loaded = co_await req.load();
    CHECK(loaded);
    req.add_date(std::chrono::system_clock::from_time_t(1651236587));
    Stream x = co_await req.send();
    co_await x.write("<html><body>It's works</body></html>");
    co_await x.write_eof();
    CHECK(out == "HTTP/1.0 200 OK\r\nDate: Fri, 29 Apr 2022 12:49:47 GMT\r\nServer: CoroServer 1.0 (C++20)\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n\r\n<html><body>It's works</body></html>");

}

coro::async<void> test_GET_http11() {

    std::string out;
    auto s = TestStream<50>::create({"GET /path HTTP/1.1\r\nHost: example.com\r\nX-Header: test\r\nX-Header2 : test2\r\n\r\n"}, &out);

    ServerRequest req(s);
    bool loaded = co_await req.load();
    CHECK(loaded);
    CHECK_EQUAL(req.get_host(),"example.com");
    CHECK_EQUAL(req.get_path(),"/path");
    CHECK(req.get_method() == Method::GET);
    CHECK(req.get_version() == Version::http1_1);
    CHECK(req.keep_alive()== true);
    req.add_date(std::chrono::system_clock::from_time_t(1651236587))
       .caching(1234)
       .no_buffering()
       .last_modified(std::chrono::system_clock::from_time_t(1651236588))
       ("X-Test",123);

    req.content_type(ContentType::text_html_utf8);
    co_await req.send("<html><body>It's works</body></html>");
    CHECK(out == "HTTP/1.1 200 OK\r\nDate: Fri, 29 Apr 2022 12:49:47 GMT\r\nCache-Control: max-age=1234\r\nX-Accel-Buffering: no\r\nLast-Modified: Fri, 29 Apr 2022 12:49:48 GMT\r\nX-Test: 123\r\nContent-Type: text/html;charset=utf-8\r\nContent-Length: 36\r\nServer: CoroServer 1.0 (C++20)\r\n\r\n<html><body>It's works</body></html>");
}

coro::async<void> test_GET_http11_infstrm() {

    std::string out;
    auto s = TestStream<50>::create({"GET /path HTTP/1.1\r\nHost: example.com\r\n\r\n"}, &out);

    ServerRequest req(s);
    bool loaded = co_await req.load();
    CHECK(loaded);
    req.add_date(std::chrono::system_clock::from_time_t(1651236587));
    Stream x = co_await req.send();
    co_await x.write("<html><body>It's works</body></html>");
    co_await x.write_eof();
    CHECK(out == "HTTP/1.1 200 OK\r\nDate: Fri, 29 Apr 2022 12:49:47 GMT\r\nServer: CoroServer 1.0 (C++20)\r\nContent-Type: application/octet-stream\r\nTransfer-Encoding: chunked\r\n\r\n24\r\n<html><body>It's works</body></html>\r\n0\r\n\r\n");
}


coro::async<void> test_POST_body() {
    auto s = TestStream<50>::create({"POST /path HTTP/1.1\r\nHost: example.com\r\nContent-Length: 18\r\n\r\n",
                            "0123456789ABCDEF\r\nExtra data"});

    ServerRequest req(s);
    bool loaded = co_await req.load();
    CHECK(loaded);
    Stream body_stream = co_await req.get_body();
    coroserver::BlockReader blk(body_stream);
    auto b = co_await blk(1000);
    CHECK_EQUAL(b, "0123456789ABCDEF\r\n");
    coroserver::BlockReader blk2(s);
    b = co_await blk2(1000);
    CHECK_EQUAL(b, "Extra data");
}

coro::async<void> test_POST_body_chunked() {
    auto s = TestStream<50>::create({"POST /path HTTP/1.1\r\nHost: example.com\r\nTransfer-Encoding: chunked\r\n\r\n",
                            "8\r\n",
                            "01234567\r",
                            "\nA\r\n89ABCDEF\r\n\r\n",
                            "0\r\n\r\nExtra data"});

    ServerRequest req(s);
    bool loaded = co_await req.load();
    CHECK(loaded);
    Stream body_stream = co_await req.get_body();
    coroserver::BlockReader blk(body_stream);
    auto b = co_await blk(1000);
    CHECK_EQUAL(b, "0123456789ABCDEF\r\n");
    coroserver::BlockReader blk2(s);
    b = co_await blk2(1000);
    CHECK_EQUAL(b, "Extra data");
}

coro::async<void> test_POST_body_expect() {
    std::string out;
    auto s = TestStream<50>::create({"POST /path HTTP/1.1\r\nHost: example.com\r\nContent-Length: 18\r\nExpect: 100-continue\r\n\r\n",
                            "0123456789ABCDEF\r\nExtra data"}, &out);

    ServerRequest req(s);
    bool loaded = co_await req.load();
    CHECK(loaded);
    Stream body_stream = co_await req.get_body();
    CHECK_EQUAL(out, "HTTP/1.1 100 Continue\r\n\r\n");
    req.add_date(std::chrono::system_clock::from_time_t(1651236587));
    co_await req.send("Done");
    CHECK(out== "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nDate: Fri, 29 Apr 2022 12:49:47 GMT\r\nContent-Length: 4\r\nServer: CoroServer 1.0 (C++20)\r\nContent-Type: application/octet-stream\r\n\r\nDone");
}

coro::async<void> test_POST_body_expect_discard() {
    std::string out;
    auto s = TestStream<50>::create({"POST /path HTTP/1.1\r\nHost: example.com\r\nContent-Length: 18\r\nExpect: 100-continue\r\n\r\n",
                            "0123456789ABCDEF\r\nExtra data"}, &out);

    ServerRequest req(s);
    bool loaded = co_await req.load();
    CHECK(loaded);
    req.add_date(std::chrono::system_clock::from_time_t(1651236587));
    co_await req.send("Done");
    CHECK(out== "HTTP/1.1 200 OK\r\nDate: Fri, 29 Apr 2022 12:49:47 GMT\r\nContent-Length: 4\r\nServer: CoroServer 1.0 (C++20)\r\nContent-Type: application/octet-stream\r\n\r\nDone");
    coroserver::BlockReader blk(s);
    auto b = co_await blk(1000);
    CHECK_EQUAL(b, "0123456789ABCDEF\r\nExtra data");

}

coro::async<void> test_POST_body_discard() {
    std::string out;
    auto s = TestStream<50>::create({"POST /path HTTP/1.1\r\nHost: example.com\r\nContent-Length: 18\r\n\r\n",
                            "0123456789ABCDEF\r\nExtra data"}, &out);

    ServerRequest req(s);
    bool loaded = co_await req.load();
    CHECK(loaded);
    req.add_date(std::chrono::system_clock::from_time_t(1651236587));
    co_await req.send("Done");
    CHECK(out== "HTTP/1.1 200 OK\r\nDate: Fri, 29 Apr 2022 12:49:47 GMT\r\nContent-Length: 4\r\nServer: CoroServer 1.0 (C++20)\r\nContent-Type: application/octet-stream\r\n\r\nDone");
    coroserver::BlockReader blk(s);
    auto b = co_await blk(1000);
    CHECK_EQUAL(b, "Extra data");

}

void test_server() {

    bool c1 =false;
    bool c2 = false;
    coroserver::http::Server server;
    server.set_handler("/a/b", [&](coroserver::http::ServerRequest &req, std::string_view vpath) {
        if (vpath.empty()) return;
        CHECK_EQUAL(vpath, "/c");
        req.add_date(std::chrono::system_clock::from_time_t(1651236587));
        req.set_status(402);
        c1 = true;
    });
    server.set_handler("/a",coroserver::http::Method::GET, [&](coroserver::http::ServerRequest &req, std::string_view vpath) -> coro::future<bool> {
        CHECK_EQUAL(vpath, "/b");
        req.add_date(std::chrono::system_clock::from_time_t(1651236587));
        c2 = true;
        return req.send("Hello world");
    });


    std::string out1;
    auto s1 = TestStream<50>::create({"GET /a/b/c HTTP/1.1\r\nHost: example.com\r\n\r\n"}, &out1);
    std::string out2;
    auto s2 = TestStream<50>::create({"GET /a/b HTTP/1.1\r\nHost: example.com\r\n\r\n"}, &out2);
    std::string out3;
    auto s3 = TestStream<50>::create({"POST /a/c HTTP/1.1\r\nHost: example.com\r\nContent-Length: 10\r\n\r\n0123456789GET /unknown HTTP/1.1\r\nHost: example.com\r\n\r\n",
                                "POST /a/c HTTP/1.1\r\nTransfer-Encoding: chunked\r\nContent-Length: 10\r\n\r\n0123456789"}, &out3);
    server.serve_req(s1).wait();
    server.serve_req(s2).wait();
    server.serve_req(s3).wait();

    CHECK_EQUAL(out1 , "HTTP/1.1 402 Payment Required\r\nDate: Fri, 29 Apr 2022 12:49:47 GMT\r\n"
                  "Content-Type: application/xhtml+xml\r\nContent-Length: 299\r\nServer: CoroServer 1.0 (C++20)\r\n"
                  "\r\n"
                  "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                  "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">"
                  "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>402 Payment Required</title></head><body><h1>402 Payment Required</h1></body></html>");

    CHECK(out2 == "HTTP/1.1 200 OK\r\nDate: Fri, 29 Apr 2022 12:49:47 GMT\r\nContent-Length: 11\r\nServer: CoroServer 1.0 (C++20)\r\nContent-Type: application/octet-stream\r\n\r\nHello world");
    CHECK_EQUAL(out3.find("405 Method"),9);
    CHECK_EQUAL(out3.find("Allow: GET\r\n"),33);
    CHECK_BETWEEN(480,out3.find("404 Not"),490);
    CHECK_BETWEEN(915,out3.find("400 Bad"),935);
    CHECK(c1);
    CHECK(c2);
}

void testHeaderParser() {

    http::ForwardedHeader f("for=12.34.56.78; by=\"aaa;bbb\"; proto = https; proto = \"http\"");
    CHECK_EQUAL(f.for_client, "12.34.56.78");
    CHECK_EQUAL(f.by, "aaa;bbb");
    CHECK_EQUAL(f.proto, "https");

}

int main() {
    testHeaderParser();
    test_GET_http10().join();
    test_GET_http10_infstrm().join();
    test_GET_http11().join();
    test_GET_http11_infstrm().join();
    test_POST_body().join();
    test_POST_body_chunked().join();
    test_POST_body_expect().join();
    test_POST_body_expect_discard().join();
    test_POST_body_discard().join();
    test_server();
}

