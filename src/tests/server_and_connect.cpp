#include "check.h"

#include <coroserver/context.h>
#include <coroserver/stream.h>

using namespace coroserver;

void check1() {
    Context ctx(2);

    auto addrs_listen = PeerName::lookup("127.0.0.1", "*");
    auto listening = ctx.accept(std::move(addrs_listen));

    auto addrs_connect = PeerName::lookup("localhost", addrs_listen[0].get_port());

    auto wtconn1 = listening();


    auto connecting = ctx.connect(addrs_connect);

    Stream s = connecting;

    Stream r = wtconn1;
}
void check2() {
    Context ctx(2);

    auto addrs_listen = PeerName::lookup("[::1]", "*");
    auto listening = ctx.accept(std::move(addrs_listen));
    auto addrs_connect = PeerName::lookup("localhost", addrs_listen[0].get_port());
    auto wtconn1 = listening();


    auto connecting = ctx.connect(addrs_connect);

    Stream s = connecting;

    Stream r = wtconn1;
}

int main() {

    check1();
    check2();

}
