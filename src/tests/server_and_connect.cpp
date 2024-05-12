#include "check.h"

#include <coroserver/context.h>
#include <coroserver/stream.h>

using namespace coroserver;

void check1() {
    Context ctx(1);

    auto addrs_listen = PeerName::lookup("127.0.0.1", "*");
    auto listening = ctx.accept(std::move(addrs_listen));

    auto addrs_connect = PeerName::lookup("127.0.0.1", addrs_listen[0].get_port());

    auto wtconn1 = listening();
    wtconn1.start();


    auto connecting = ctx.connect(addrs_connect);

    Stream s = connecting;

    Stream r = wtconn1;
}
void check2() {
    Context ctx(1);

    auto addrs_listen = PeerName::lookup("[::1]", "*");
    auto listening = ctx.accept(std::move(addrs_listen));
    auto addrs_connect = PeerName::lookup("localhost", addrs_listen[0].get_port());
    auto wtconn1 = listening();
    wtconn1.start();


    auto connecting = ctx.connect(addrs_connect);

    Stream s = connecting;

    Stream r = wtconn1;
}

void check3() {
    Context ctx(1);

    auto addrs_connect = PeerName::lookup("localhost:12345");

    try {
        auto connecting = ctx.connect(addrs_connect);

        Stream s = connecting;
        CHECK(false);
    } catch (...) {
        CHECK(true);
    }
}

int main() {

//    check1();
    check2();
//    check3();

}
