#include "client.hpp"
#include "basic_stream.hpp"

namespace coroserver {



Stream connect(Context ctx, std::string host,std::string def_port) {
    auto h = ctx.connect(std::move(host), std::move(def_port));
    return Stream(std::make_shared<BasicStream>(std::move(ctx), h));
}

Stream connect(Context ctx, SpecialDevice specdev) {
    auto h = ctx.connect(specdev);
    return Stream(std::make_shared<BasicStream>(std::move(ctx), h));

}

}
