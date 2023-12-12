#include "check.h"
#include "test_stream.h"


#include "../coroserver/http_client.h"
#include "../coroserver/stream_utils.h"

using namespace coroserver;

coro::async<void> test_create_request() {

    std::string out;
    Stream s = TestStream<100>::create({"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nxyz"}, &out);

    http::ClientRequest request({s,http::Method::POST, "www.example.com", "/test/path"});
    request("User-Agent","Test");
    Stream body = co_await request.begin_body(3);
    co_await body.write("abc");
    Stream response = co_await request.send();
    BlockReader<Stream> bs(response);
    auto res = co_await bs.read(10);
    CHECK_EQUAL(out,"POST /test/path HTTP/1.1\r\nHost: www.example.com\r\nUser-Agent: Test\r\nContent-Length: 3\r\n\r\nabc");
    CHECK_EQUAL(res,"xyz");
}

coro::async<void> test_create_empty_request() {

    std::string out;
    Stream s = TestStream<100>::create({"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nxyz"}, &out);

    http::ClientRequest request({s,http::Method::DELETE, "www.example.com", "/test/path"});
    request("User-Agent","Test");
    Stream response = co_await request.send();
    BlockReader<Stream> bs(response);
    auto res = co_await bs.read(10);
    CHECK_EQUAL(out,"DELETE /test/path HTTP/1.1\r\nHost: www.example.com\r\nUser-Agent: Test\r\nContent-Length: 0\r\n\r\n");
    CHECK_EQUAL(res,"xyz");
}

coro::async<void> test_GET_request() {
    std::string out;
    Stream s = TestStream<100>::create({"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nxyz"}, &out);

    http::ClientRequest request({s,http::Method::GET, "www.example.com", "/test/path"});
    request("User-Agent","Test");
    Stream response = co_await request.send();
    BlockReader<Stream> bs(response);
    auto res = co_await bs.read(10);
    CHECK_EQUAL(out,"GET /test/path HTTP/1.1\r\nHost: www.example.com\r\nUser-Agent: Test\r\n\r\n");
    CHECK_EQUAL(res,"xyz");

}

coro::async<void> test_request_100_cont() {

    std::string out;
    Stream s = TestStream<100>::create({"HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 202 OK\r\nContent-Length: 3\r\n\r\nxyz"}, &out);

    http::ClientRequest request({s,http::Method::POST, "www.example.com", "/test/path"});
    request("User-Agent","Test");
    request.expect100continue();
    Stream body = co_await request.begin_body(3);
    CHECK_EQUAL(request.get_status(),100);
    co_await body.write("abc");
    Stream response = co_await request.send();
    BlockReader<Stream> bs(response);
    auto res = co_await bs.read(10);
    CHECK_EQUAL(request.get_status(),202);
    CHECK_EQUAL(out,"POST /test/path HTTP/1.1\r\nHost: www.example.com\r\nUser-Agent: Test\r\nContent-Length: 3\r\nExpect: 100-continue\r\n\r\nabc");
    CHECK_EQUAL(res,"xyz");
}

coro::async<void> test_request_100_cont_error() {

    std::string out;
    Stream s = TestStream<100>::create({"HTTP/1.1 403 Error\r\nContent-Length: 3\r\n\r\nxyz"}, &out);

    http::ClientRequest request({s,http::Method::POST, "www.example.com", "/test/path"});
    request("User-Agent","Test");
    request.expect100continue();
    Stream body = co_await request.begin_body(3);
    CHECK_EQUAL(request.get_status(),403);
    Stream response = co_await request.send();
    BlockReader<Stream> bs(response);
    auto res = co_await bs.read(10);
    CHECK_EQUAL(out,"POST /test/path HTTP/1.1\r\nHost: www.example.com\r\nUser-Agent: Test\r\nContent-Length: 3\r\nExpect: 100-continue\r\n\r\n");
    CHECK_EQUAL(res,"xyz");
}

coro::async<void> test_client_1() {
    std::string out;
    http::Client client({"TestClient", [&](std::string_view host){
        CHECK_EQUAL(host, "www.example.com:123");
        return TestStream<100>::create({"HTTP/1.1 403 Error\r\nContent-Length: 3\r\n\r\nxyz"}, &out);

    },nullptr});

    http::ClientRequestParams params = co_await client.open(http::Method::GET, "http://www.example.com:123/path/file?query=value");
    CHECK_EQUAL(params.auth, "");
    CHECK_EQUAL(params.host, "www.example.com:123");
    CHECK(params.method == http::Method::GET);
    CHECK_EQUAL(params.path, "/path/file?query=value");
    CHECK_EQUAL(params.user_agent, "TestClient");
    CHECK(params.ver == http::Version::http1_1);

    http::ClientRequest req(std::move(params));
    auto response = co_await req.send();

    CHECK_EQUAL(req.get_status(), 403);

    std::cout << out << std::endl;


}


int main() {
    test_create_request().join();
    test_create_empty_request().join();
    test_GET_request().join();
    test_request_100_cont().join();
    test_request_100_cont_error().join();
    test_client_1().join();
}
