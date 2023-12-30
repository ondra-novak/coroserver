#include "../coroserver/umq_stream_connection.h"
#include "../coroserver/context.h"
#include "../coroserver/umq_peer.h"
#include "../coroserver/tee.h"
#include "check.h"


#include "test_stream.h"


coro::async<void> connect_pair(coroserver::Context &ctx, coroserver::umq::Peer &p1, coroserver::umq::Peer &p2) {
    auto streams = ctx.create_pair();
    auto conn1 = std::make_unique<coroserver::umq::StreamConnection>(
            create_tee(streams.first, coroserver::TeePeekToStream(std::cout)));
    auto conn2 = std::make_unique<coroserver::umq::StreamConnection>(streams.second);
    auto f1 = p1.listen(std::move(conn1));
    auto f2 = p2.connect(std::move(conn2), coroserver::umq::Peer::Payload("abc"));
    coroserver::umq::Peer::Request req = co_await f1;
    req.response(coroserver::umq::Peer::Payload("xyz"));
    coroserver::umq::Peer::Payload pl = co_await f2;
    CHECK_EQUAL(req.text, "abc");
    CHECK_EQUAL(pl.text, "xyz");
}



void test_hello(coroserver::Context &ctx) {
    coroserver::umq::Peer p1;
    coroserver::umq::Peer p2;
    connect_pair(ctx, p1, p2).join();

}

coro::async<void> test_rpc(coroserver::Context &ctx) {
    coroserver::umq::Peer p1;
    coroserver::umq::Peer p2;
    co_await connect_pair(ctx, p1, p2);
    auto f1 = p1.receive_request();
    auto f2 = p2.send_request(std::string_view("ahoj"));
    auto f3 = p2.send_request(std::string_view("ahoj2"));
    coroserver::umq::Peer::Request req = co_await f1;
    coroserver::umq::Peer::Request req2 = co_await p1.receive_request();
    req.response("cau");
    req2.response("cau2");

    coroserver::umq::Peer::Payload resp = co_await f2;
    coroserver::umq::Peer::Payload resp2 = co_await f3;
    CHECK_EQUAL(req.text,"ahoj");
    CHECK_EQUAL(resp.text,"cau");
    CHECK_EQUAL(req2.text,"ahoj2");
    CHECK_EQUAL(resp2.text,"cau2");


}


int main() {
    coroserver::Context ctx(1);
    test_hello(ctx);
    test_rpc(ctx).join();



}
