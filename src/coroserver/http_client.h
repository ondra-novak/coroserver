/*
 * http_client.h
 *
 *  Created on: 3. 5. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_HTTP_CLIENT_H_
#define SRC_COROSERVER_HTTP_CLIENT_H_
#include "http_client_request.h"

#include <cocls/function.h>

#include "io_context.h"
namespace coroserver{

namespace http {

using ConnectionFactory = std::function<cocls::future<Stream>(std::string_view)>;

ConnectionFactory connectionFactory(ContextIO ctx, int timeout_ms, TimeoutSettings tms) {
    return [ctx  = std::move(ctx), timeout_ms, tms](std::string_view host) mutable ->cocls::future<Stream> {
        auto list = PeerName::lookup(host, "80");
        return ctx.connect(std::move(list), timeout_ms, tms);
    };
}


///Http client creates http connections
/**
 * This object actually doesn't implement http client, it is implemented by
 * ClientRequest. This object helps to parse URL and construct params
 * for this object
 *
 * The function open actually return ClientRequestParams, because the
 * operation is asynchronous and ClientRequest is not movable.
 * But it is possible to construct ClientRequest using ClientRequestParams
 * retrieved asynchronously
 *
 */
class HttpClient {
public:

    ///Configuration of the client
    struct Config {
        ///User agent string
        std::string user_agent = "Coroserver Http Client (https://github.com/ondra-novak/coroserver)";
        ///Factory to construct http connection - use http::connectionFactory to construct such factory
        ConnectionFactory http;
        ///Factory to construct https connection - use ssl::connectionFactory to construct secure factory
        ConnectionFactory https;
        ///Default version
        Version ver = Version::http1_1;
    };


    ///Create http client
    HttpClient(Config cfg);

    ///Initialize ClientRequestParams which can be used to initialize ClientRequest
    /**
     * @param method method
     * @param url url. Url can contain protocol, host, path. The host can contain authorization
     * @return ClientRequestParams
     */
    cocls::future<ClientRequestParams> open(Method method, std::string_view url);


protected:

    Config _cfg;
};


}


}



#endif /* SRC_COROSERVER_HTTP_CLIENT_H_ */
