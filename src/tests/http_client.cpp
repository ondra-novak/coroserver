#include "../coroserver/context.hpp"
#include "../coroserver/client.hpp"
#include "../coroserver/http_client_request.hpp"
#include <basic_coro/when_all.hpp>
#include "check.h"

using namespace coroserver;


constexpr std::string_view mock_server = "echo.free.beeceptor.com";

awaitable<void> test_get(Context &ctx) {

    Stream s = connect(ctx,std::string(mock_server), "80");
    http::ClientRequest req(s);
    req.open(http::Method::GET, "/sample-request?source=coroserver");
    req.add_header("Host", mock_server);
    req.add_header("Connection","close");
    std::optional<Stream> resp = co_await req.send().as_optional();
    CHECK(resp.has_value());
    std::string buff;
    co_await resp->read_block(buff, 65536);
    std::cout << buff << std::endl;

}

int main() {

    Context ctx = Context::create(1);
    auto awt = test_get(ctx);
    coro::when_all(awt).wait();


}
