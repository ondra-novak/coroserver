#pragma once
#ifndef SRC_COROSERVER_HTTPS_CLIENT_H_
#define SRC_COROSERVER_HTTPS_CLIENT_H_


#include "http_client.h"
#include "ssl_stream.h"

namespace coroserver {


namespace https {


///Https connection factory
inline ConnectionFactory connectionFactory(ContextIO ioctx, ssl::Context sslctx, int timeout_ms, TimeoutSettings tms) {
    return [ioctx  = std::move(ioctx), sslctx = std::move(sslctx), timeout_ms, tms](std::string_view host) mutable ->cocls::future<Stream> {
        auto list = PeerName::lookup(host, "443");
        Stream s = co_await ioctx.connect(std::move(list), timeout_ms, tms);
        co_return ssl::Stream::connect(s, sslctx, std::string(host));
    };
}


///Implements https client (it is also supports http protocol)
class Client: public http::Client {
public:

    Client(ContextIO ioctx, ssl::Context sslctx, std::string user_agent, int timeout_ms=ContextIO::defaultTimeout , TimeoutSettings tms={ContextIO::defaultTimeout,ContextIO::defaultTimeout})
        :http::Client({
            std::move(user_agent),
            http::connectionFactory(ioctx, timeout_ms, tms),
            connectionFactory(std::move(ioctx), std::move(sslctx), timeout_ms, tms)
    }) {}

};



}

}



#endif /* SRC_COROSERVER_HTTPS_CLIENT_H_ */
