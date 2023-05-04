#include "check.h"
#include "test_stream.h"
#include <coroserver/http_client_request.h>

using namespace coroserver;

cocls::async<void> test_create_request() {

    std::string out;
    Stream s = TestStream<100>::create({"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nxyz"}, &out);

    http::ClientRequest request({s,http::Method::POST, "www.example.com", "/test/path"});
    request("User-Agent","Test");
    Stream body = co_await request.begin_body(3);
    co_await body.write("abc");
    Stream response = co_await request.send();
    std::string res;
    co_await response.read_block(res, 10);
    CHECK_EQUAL(out,"POST /test/path HTTP/1.1\r\nHost: www.example.com\r\nUser-Agent: Test\r\nContent-Length: 3\r\n\r\nabc");
    CHECK_EQUAL(res,"xyz");
}

cocls::async<void> test_create_empty_request() {

    std::string out;
    Stream s = TestStream<100>::create({"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nxyz"}, &out);

    http::ClientRequest request({s,http::Method::DELETE, "www.example.com", "/test/path"});
    request("User-Agent","Test");
    Stream response = co_await request.send();
    std::string res;
    co_await response.read_block(res, 10);
    CHECK_EQUAL(out,"DELETE /test/path HTTP/1.1\r\nHost: www.example.com\r\nUser-Agent: Test\r\nContent-Length: 0\r\n\r\n");
    CHECK_EQUAL(res,"xyz");
}

cocls::async<void> test_GET_request() {
    std::string out;
    Stream s = TestStream<100>::create({"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nxyz"}, &out);

    http::ClientRequest request({s,http::Method::GET, "www.example.com", "/test/path"});
    request("User-Agent","Test");
    Stream response = co_await request.send();
    std::string res;
    co_await response.read_block(res, 10);
    CHECK_EQUAL(out,"GET /test/path HTTP/1.1\r\nHost: www.example.com\r\nUser-Agent: Test\r\n\r\n");
    CHECK_EQUAL(res,"xyz");

}

cocls::async<void> test_request_100_cont() {

    std::string out;
    Stream s = TestStream<100>::create({"HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 202 OK\r\nContent-Length: 3\r\n\r\nxyz"}, &out);

    http::ClientRequest request({s,http::Method::POST, "www.example.com", "/test/path"});
    request("User-Agent","Test");
    request.expect100continue();
    Stream body = co_await request.begin_body(3);
    CHECK_EQUAL(request.get_status(),100);
    co_await body.write("abc");
    Stream response = co_await request.send();
    std::string res;
    co_await response.read_block(res, 10);
    CHECK_EQUAL(request.get_status(),202);
    CHECK_EQUAL(out,"POST /test/path HTTP/1.1\r\nHost: www.example.com\r\nUser-Agent: Test\r\nExpect: 100-continue\r\nContent-Length: 3\r\n\r\nabc");
    CHECK_EQUAL(res,"xyz");
}

cocls::async<void> test_request_100_cont_error() {

    std::string out;
    Stream s = TestStream<100>::create({"HTTP/1.1 403 OK\r\nContent-Length: 3\r\n\r\nxyz"}, &out);

    http::ClientRequest request({s,http::Method::POST, "www.example.com", "/test/path"});
    request("User-Agent","Test");
    request.expect100continue();
    Stream body = co_await request.begin_body(3);
    CHECK_EQUAL(request.get_status(),403);
    Stream response = co_await request.send();
    std::string res;
    co_await response.read_block(res, 10);
    CHECK_EQUAL(out,"POST /test/path HTTP/1.1\r\nHost: www.example.com\r\nUser-Agent: Test\r\nExpect: 100-continue\r\nContent-Length: 3\r\n\r\n");
    CHECK_EQUAL(res,"xyz");
}



int main() {
    test_create_request().join();
    test_create_empty_request().join();
    test_GET_request().join();
    test_request_100_cont().join();
    test_request_100_cont_error().join();
}
