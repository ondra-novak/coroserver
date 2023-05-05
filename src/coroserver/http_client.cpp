#include "http_client.h"
#include "init_by_fn.h"


namespace coroserver {



namespace http {

Client::Client(Config cfg)
    :_cfg(cfg)
{
}
Client::Client(Config cfg, Headers hdrs)
    :_cfg(cfg),_hdrs(std::make_shared<Headers>(std::move(hdrs))) {}


Client::Client(Config cfg, StaticHeaders hdrs)
    :_cfg(cfg),_hdrs(std::move(hdrs)) {

}


cocls::future<ClientRequestParams> Client::open(Method method,std::string_view url) {
    ConnectionFactory *fact = nullptr;
    if (url.compare(0, 7, "http://") == 0) {
        fact = &_cfg.http;
        url = url.substr(7);
    } else if (url.compare(0, 8, "https://") == 0) {
        fact = &_cfg.https;
        url = url.substr(8);
    } else {
        throw std::invalid_argument("Invalid protocol");
    }

    if (fact == nullptr)
        throw std::invalid_argument("Protocol is not installed");

    auto sep = url.find('/');
    std::string_view host_part;
    std::string_view path;
    if (sep == url.npos) {
        host_part = url;
        path = "/";
    } else {
        host_part = url.substr(0, sep);
        path = url.substr(sep);
    }

    sep = host_part.rfind('@');
    std::string auth;
    std::string_view host;
    if (sep != host_part.npos) {
        host = host_part.substr(sep+1);
        auto auth_view = host_part.substr(0, sep);
        auth = "Basic ";
        base64::encode(auth_view, [&](char c){return auth.push_back(c);});
    } else {
        host = host_part;
    }

    Stream s = co_await (*fact)(host);

    co_return InitByFn([&]{return ClientRequestParams {
        std::move(s),
        method,
        host,
        path,
        _cfg.user_agent,
        auth,
        _hdrs,
        _cfg.ver
    };});
}

}

}
