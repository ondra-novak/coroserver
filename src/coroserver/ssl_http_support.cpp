#include "ssl_http_support.h"

#include "http_server.h"
#include "ssl_common.h"
#include "ssl_stream.h"

namespace coroserver {

http::Server::RequestFactory http::Server::secure(ssl::Context &ctx, std::vector<PeerName> secure_addresses) {
    if (secure_addresses.empty()) {
        return [ctx](Stream s) {
            return http::ServerRequest(ssl::Stream::accept(std::move(s), ctx), true);
        };
    } else {
        return [ctx, secure_addresses = std::move(secure_addresses)](Stream s) {
            auto ifcname = s.get_interface_name();
            auto iter = std::find_if(secure_addresses.begin(), secure_addresses.end(), [&](auto p){
                return p.match(ifcname);
            });
            if (iter != secure_addresses.end()) {
                return http::ServerRequest(ssl::Stream::accept(std::move(s), ctx), true);
            } else {
                return http::ServerRequest(std::move(s));
            }

        };
    }
}



}
