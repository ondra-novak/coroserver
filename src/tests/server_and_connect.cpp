#include "check.h"

#include <coroserver/io_context.h>

using namespace coroserver;

void check1() {
    ContextIO ctx = ContextIO::create(2);

    auto addrs_listen = PeerName::lookup("127.0.0.1", "12345");
    auto addrs_connect = PeerName::lookup("localhost", "12345");

    auto listening = ctx.accept(addrs_listen);
    auto wtconn1 = listening();


    auto connecting = ctx.connect(addrs_connect);

    Stream s = connecting.join();

    Stream r = wtconn1.join();
}
void check2() {
    ContextIO ctx = ContextIO::create(2);

    auto addrs_listen = PeerName::lookup("[::1]", "12345");
    auto addrs_connect = PeerName::lookup("localhost", "12345");

    auto listening = ctx.accept(addrs_listen);
    auto wtconn1 = listening();


    auto connecting = ctx.connect(addrs_connect);

    Stream s = connecting.join();

    Stream r = wtconn1.join();
}

int main() {

    check1();
    check2();

}
