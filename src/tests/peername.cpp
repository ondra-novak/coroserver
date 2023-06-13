#include <coroserver/peername.h>
#include "check.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <algorithm>
using namespace coroserver;


int main(int argc, char **argv) {
    {
        auto peer = PeerName::lookup("127.0.0.1:12345");
        CHECK_EQUAL(peer.size(),1);
        auto ipv4 = peer[0].get_ipv4();
        CHECK_NOT_EQUAL(ipv4 , nullptr);
        CHECK_EQUAL(ipv4->addr , 0x0100007F);
        CHECK_EQUAL(ipv4->port , 0x3930);
        CHECK_EQUAL(peer[0].to_string(),std::string("127.0.0.1:12345"));
        peer[0].use_sockaddr([](const sockaddr *addr, socklen_t slen){
            CHECK_EQUAL(addr->sa_family , AF_INET);
            const sockaddr_in *in = reinterpret_cast<const sockaddr_in *>(addr);
            CHECK_EQUAL(in->sin_port, 0x3930);
            CHECK_EQUAL(in->sin_addr.s_addr, 0x0100007F);
            CHECK_EQUAL(slen, sizeof(sockaddr_in));
        });
    }
    {
        auto peer = PeerName::lookup("[::1]:12345");
        CHECK_EQUAL(peer.size(),1);
        auto ipv6 = peer[0].get_ipv6();
        CHECK_NOT_EQUAL(ipv6 , nullptr);
        CHECK_EQUAL(peer[0].to_string(),std::string("[0:0:0:0:0:0:0:1]:12345"));
        auto peer2 = peer;
        CHECK(peer == peer2);
    }
    {
            auto peer = PeerName::lookup("[0123:4567:7890:1234:5678:9012:3456:7890]:11111");
            CHECK_EQUAL(peer.size(),1);
            peer[0].use_sockaddr([](const sockaddr *addr, socklen_t slen){
                CHECK_EQUAL(addr->sa_family , AF_INET6);
                const sockaddr_in6 *in = reinterpret_cast<const sockaddr_in6 *>(addr);
                CHECK_EQUAL(in->sin6_port, 26411);
                CHECK_EQUAL(in->sin6_addr.__in6_u.__u6_addr16[0] , 0x2301);
                CHECK_EQUAL(in->sin6_addr.__in6_u.__u6_addr16[1] , 0x6745);
                CHECK_EQUAL(in->sin6_addr.__in6_u.__u6_addr16[2] , 0x9078);
                CHECK_EQUAL(in->sin6_addr.__in6_u.__u6_addr16[3] , 0x3412);
                CHECK_EQUAL(in->sin6_addr.__in6_u.__u6_addr16[4] , 0x7856);
                CHECK_EQUAL(in->sin6_addr.__in6_u.__u6_addr16[5] , 0x1290);
                CHECK_EQUAL(in->sin6_addr.__in6_u.__u6_addr16[6] , 0x5634);
                CHECK_EQUAL(in->sin6_addr.__in6_u.__u6_addr16[7] , 0x9078);
                CHECK_EQUAL(slen, sizeof(sockaddr_in6));
            });
        }
    {
        CHECK_EXCEPTION(PeerName::LookupException,
                auto peer = PeerName::lookup("noexist_domain:12345")
        );

    }
    {
        auto peer = PeerName::lookup("unix:/tmp/socket.sock:ugo");
        CHECK_EQUAL(peer.size(),1);
        auto un = peer[0].get_unix();
        CHECK_NOT_EQUAL(un , nullptr);
        CHECK_EQUAL(un->path.string() , "/tmp/socket.sock");
        CHECK_EQUAL(un->perms, 0666);
        peer[0].use_sockaddr([](const sockaddr *addr, socklen_t slen){
            CHECK_EQUAL(addr->sa_family , AF_UNIX);
            const sockaddr_un *un = reinterpret_cast<const sockaddr_un *>(addr);
            CHECK_EQUAL(std::string_view(un->sun_path) , "/tmp/socket.sock");
            CHECK_EQUAL(slen, sizeof(sockaddr_un));
        });
    }
    {
          auto peer = PeerName::lookup("127.0.0.1:12345 unix:/tmp/socket.sock:ugo noexist_domain:22351");
          CHECK_EQUAL(peer.size(),3);
          CHECK(peer[0] == PeerName::lookup("127.0.0.1:12345")[0]);
          CHECK(peer[1] == PeerName::lookup("unix:/tmp/socket.sock:ugo")[0]);
          CHECK_EQUAL(peer[2].valid(), false);
      }



}




