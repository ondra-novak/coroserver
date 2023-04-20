#include "check.h"
#include "test_stream.h"
#include <coroserver/http_server_request.h>

using namespace coroserver;
using namespace coroserver::http;



cocls::async<void> test_GET_http10() {

    std::string out;
    auto s = TestStream<>::create({"GET /path HTTP/1.0\r\nHost: example.com\r\nX-Header: ","test\r\nX-Header2 : test2\r\n","\r\n"}, &out);

    ServerRequest req(s);
    bool loaded = co_await req.load();
    CHECK(loaded);
    CHECK_EQUAL(req.get_host(),"example.com");
    CHECK_EQUAL(req.get_path(),"/path");
    CHECK(req.get_method() == Method::GET);
    CHECK(req.keep_alive()== false);
    CHECK(req.get_version() == Version::http1_0);
    req.add_date(std::chrono::system_clock::from_time_t(1651236587));
    co_await req.send(ContentType::text_html_utf8, "<html><body>It's works</body></html>");
    CHECK(out == "HTTP/1.0 200 OK\r\nDate: Fri, 29 Apr 2022 12:49:47 GMT\r\nContent-Type: text/html;charset=utf-8\r\nContent-Length: 36\r\nServer: CoroServer 1.0 (C++20)\r\nConnection: close\r\n\r\n<html><body>It's works</body></html>");
}
cocls::async<void> test_GET_http10_infstrm() {

    std::string out;
    auto s = TestStream<>::create({"GET /path HTTP/1.0\r\nHost: example.com\r\nX-Header: ","test\r\nX-Header2 : test2\r\n","\r\n"}, &out);

    ServerRequest req(s);
    bool loaded = co_await req.load();
    CHECK(loaded);
    req.add_date(std::chrono::system_clock::from_time_t(1651236587));
    Stream x = co_await req.send();
    co_await x.write("<html><body>It's works</body></html>");
    co_await x.write_eof();
    CHECK(out == "HTTP/1.0 200 OK\r\nDate: Fri, 29 Apr 2022 12:49:47 GMT\r\nServer: CoroServer 1.0 (C++20)\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: close\r\n\r\n<html><body>It's works</body></html>");

}

cocls::async<void> test_GET_http11() {

    std::string out;
    auto s = TestStream<>::create({"GET /path HTTP/1.1\r\nHost: example.com\r\nX-Header: test\r\nX-Header2 : test2\r\n\r\n"}, &out);

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

    co_await req.send(ContentType::text_html_utf8, "<html><body>It's works</body></html>");
    CHECK(out == "HTTP/1.1 200 OK\r\nDate: Fri, 29 Apr 2022 12:49:47 GMT\r\nCache-Control: max-age=1234\r\nX-Accel-Buffering: no\r\nLast-Modified: Fri, 29 Apr 2022 12:49:48 GMT\r\nX-Test: 123\r\nContent-Type: text/html;charset=utf-8\r\nContent-Length: 36\r\nServer: CoroServer 1.0 (C++20)\r\n\r\n<html><body>It's works</body></html>");
}

cocls::async<void> test_GET_http11_infstrm() {

    std::string out;
    auto s = TestStream<>::create({"GET /path HTTP/1.1\r\nHost: example.com\r\n\r\n"}, &out);

    ServerRequest req(s);
    bool loaded = co_await req.load();
    CHECK(loaded);
    req.add_date(std::chrono::system_clock::from_time_t(1651236587));
    Stream x = co_await req.send();
    co_await x.write("<html><body>It's works</body></html>");
    co_await x.write_eof();
    CHECK(out == "HTTP/1.1 200 OK\r\nDate: Fri, 29 Apr 2022 12:49:47 GMT\r\nServer: CoroServer 1.0 (C++20)\r\nContent-Type: text/plain; charset=utf-8\r\nTransfer-Encoding: chunked\r\n\r\n24\r\n<html><body>It's works</body></html>\r\n0\r\n\r\n");
}


cocls::async<void> test_POST_body() {
    auto s = TestStream<>::create({"POST /path HTTP/1.1\r\nHost: example.com\r\nContent-Length: 18\r\n\r\n",
                            "0123456789ABCDEF\r\nExtra data"});
                       
    ServerRequest req(s);
    bool loaded = co_await req.load();
    CHECK(loaded);
    Stream body_stream = co_await req.get_body();
    std::string b;
    co_await body_stream.read_block(b, 1000);
    CHECK_EQUAL(b, "0123456789ABCDEF\r\n");
    co_await s.read_block(b, 1000);
    CHECK_EQUAL(b, "Extra data");
}

cocls::async<void> test_POST_body_chunked() {
    auto s = TestStream<>::create({"POST /path HTTP/1.1\r\nHost: example.com\r\nTransfer-Encoding: chunked\r\n\r\n",
                            "8\r\n",
                            "01234567\r",
                            "\nA\r\n89ABCDEF\r\n\r\n",
                            "0\r\n\r\nExtra data"});
                       
    ServerRequest req(s);
    bool loaded = co_await req.load();
    CHECK(loaded);
    Stream body_stream = co_await req.get_body();
    std::string b;
    co_await body_stream.read_block(b, 1000);
    CHECK_EQUAL(b, "0123456789ABCDEF\r\n");
    co_await s.read_block(b, 1000);
    CHECK_EQUAL(b, "Extra data");
}

cocls::async<void> test_POST_body_expect() {
    std::string out;
    auto s = TestStream<>::create({"POST /path HTTP/1.1\r\nHost: example.com\r\nContent-Length: 18\r\nExpect: 100-continue\r\n\r\n",
                            "0123456789ABCDEF\r\nExtra data"}, &out);
                       
    ServerRequest req(s);
    bool loaded = co_await req.load();
    CHECK(loaded);
    Stream body_stream = co_await req.get_body();
    CHECK_EQUAL(out, "HTTP/1.1 100 Continue\r\n\r\n");
}


int main() {
    test_GET_http10().join();
    test_GET_http10_infstrm().join();
    test_GET_http11().join();
    test_GET_http11_infstrm().join();
    test_POST_body().join();    
    test_POST_body_chunked().join();    
    test_POST_body_expect().join();    
}
