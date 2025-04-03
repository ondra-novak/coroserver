#include "context.hpp"
#include "handle_hash_map.hpp"
#include "stream_state.h"

namespace coroserver {

class ContextImpl;


enum class HandleType {
    timer,
    server,
    socket,
    pipes
};

struct TwoCoros {
    coro::prepared_coro a = {};
    coro::prepared_coro b = {};
    TwoCoros() = default;
    TwoCoros(coro::prepared_coro a):a(std::move(a)) {}
    TwoCoros(coro::prepared_coro a, coro::prepared_coro b)
        :a(std::move(a)), b(std::move(b)) {}
};



class AbstractHandleData {
public:

    AbstractHandleData(HandleType type):_type(type) {}

    HandleType get_type() const {return _type;}


    template<typename Fn>
    auto visit(Fn &&fn);
    template<typename Fn>
    auto visit(Fn &&fn) const;


    StreamState get_state() const {return _was_shutdown?StreamState::closed:StreamState::active;}
protected:
    HandleType _type;
    bool _was_shutdown = false;
};

struct HandleDataDeleter {
    void operator()(AbstractHandleData *p);
};


using PHandleData = std::unique_ptr<AbstractHandleData, HandleDataDeleter>;


}