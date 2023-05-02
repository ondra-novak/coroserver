#include "ssl_http_support.h"

#include "http_server.h"
#include "ssl_common.h"
#include "ssl_stream.h"

namespace coroserver {

http::Server::RequestFactory http::Server::secure(ssl::Context &ctx, int group_id , bool mask) {
    if (!group_id) {
        return [ctx](Stream s) {
            return http::ServerRequest(ssl::Stream::accept(std::move(s), ctx), true);
        };
    } else if (mask){
        return [ctx, group_id](Stream s) {
            auto id = s.get_peer_name().get_group_id();
            if (id == group_id) {
                return http::ServerRequest(ssl::Stream::accept(std::move(s), ctx), true);
            } else {
                return http::ServerRequest(std::move(s));
            }
        };
    } else {
        return [ctx, group_id](Stream s) {
            auto id = s.get_peer_name().get_group_id();
            if ((id & group_id) == group_id) {
                return http::ServerRequest(ssl::Stream::accept(std::move(s), ctx), true);
            } else {
                return http::ServerRequest(std::move(s));
            }
        };

    }
}



}
