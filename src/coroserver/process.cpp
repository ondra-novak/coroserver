#include "stream.hpp"
#include "basic_stream.hpp"
#include "process.hpp"

namespace coroserver {


Stream spawn_process(Context ctx, std::string_view path, std::span<const std::string_view> args, const Environment &env ) {
    auto h = ctx.create_process(path, args, env);
    return Stream(std::make_shared<BasicStream>(std::move(ctx), h));
}

bool terminate_process(Stream stream) {
    auto ptr = std::dynamic_pointer_cast<BasicStream>(stream.get_handle());
    if (ptr) {
        auto h = ptr->get_handle();
        Context ctx = stream.get_context();
        return ctx.terminate_process(h);
    }
    return false;
}

coro::awaitable<int> get_process_exit_status(Stream stream, std::chrono::system_clock::time_point tp) {
    auto ptr = std::dynamic_pointer_cast<BasicStream>(stream.get_handle());
    if (ptr) {
        auto h = ptr->get_handle();
        Context ctx = stream.get_context();
        return ctx.get_process_exit_status(h, tp);
    }
    return false;


}


}

