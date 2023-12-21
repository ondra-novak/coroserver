#pragma once
#ifndef SRC_COROSERVER_HTTPS_CLIENT_H_
#define SRC_COROSERVER_HTTPS_CLIENT_H_


#include "http_client.h"
#include "ssl_stream.h"

namespace coroserver {


namespace https {


///Https connection factory
inline ConnectionFactory connectionFactory(Context &ioctx, ssl::Context sslctx, TimeoutSettings::Dur connect_timeout, TimeoutSettings tms) {
    return [&ioctx, sslctx = std::move(sslctx), connect_timeout, tms](std::string_view host) mutable ->coro::future<Stream> {
        auto list = PeerName::lookup(host, "443");
        Stream s = co_await ioctx.connect(std::move(list), connect_timeout, tms);
        co_return ssl::Stream::connect(s, sslctx, std::string(host));
    };
}


///Implements https client (it is also supports http protocol)
class Client: public http::Client {
public:

    Client(Context &ioctx, ssl::Context sslctx, std::string user_agent, TimeoutSettings::Dur connect_timeout=defaultConnectTimeout, TimeoutSettings tms=defaultTimeout)
        :http::Client({
            std::move(user_agent),
            http::connectionFactory(ioctx, connect_timeout, tms),
            connectionFactory(ioctx, std::move(sslctx), connect_timeout, tms)
    }) {}

};



}

}



#endif /* SRC_COROSERVER_HTTPS_CLIENT_H_ */
