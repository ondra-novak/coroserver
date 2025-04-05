#include "client.hpp"
#include "basic_stream.hpp"

namespace coroserver {



Stream connect(Context ctx, std::string host,std::string def_port) {
    auto h = ctx.connect(std::move(host), std::move(def_port));
    return Stream(std::make_shared<BasicStream>(std::move(ctx), h));
}

Stream connect_stdinout(Context ctx) {
    auto h = ctx.connect_stdinout();
    return Stream(std::make_shared<BasicStream>(std::move(ctx), h));
}

}
