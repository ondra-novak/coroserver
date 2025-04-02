#pragma once

#include "context.hpp"

namespace coroserver {


///Enables to sleep on coroutine and simple scheduling while using IO Context
class Timer {
public:

    ///Constructor
    /**
     * @param ctx IO context
     * @param h timer's handle
     * 
     * @note It is more convenient to use Timer::create()
     */
    Timer(Context ctx, Context::Handle h):_ctx(std::move(ctx)), _h(h) {}

    ///Construct unitialized timer variable - cannot be used until assigned
    Timer() = default;

    ///Timer object is movable
    Timer(Timer &&other):_ctx(std::move(other._ctx)),_h(std::move(other._h)) {other._h = Context::null_handle;}
    ///Timer object is assignable (move)
    Timer &operator=(Timer &&other) {
        if (this != &other) {
            if (_h != Context::null_handle) {
                _ctx.close(_h);
            }
            _h = other._h;
            other._h = Context::null_handle;
        }
        return *this;
    }

    ///dtor
    ~Timer() {
        if (_h != Context::null_handle) _ctx.close(_h);
    }

    ///Shutdowns timer instance causing interruption of current sleep
    /** Once the timer is shut down, you cannot reenable it, you must recreate the timer object */ 
    void shutdown() {
        _ctx.shutdown(_h);
    }

    ///Sleep on the timer object
    /**
     * @param tp time point
     * @return awaitable object
     * @retval true success
     * @retval false interrupted (was shut down)
     */
    coro::awaitable<bool> sleep_until(std::chrono::system_clock::time_point tp) {
        return _ctx.sleep(_h,tp);
    }
    ///Sleep on the timer object
    /**
     * @param dur sleep duration
     * @return awaitable object
     * @retval true success
     * @retval false interrupted (was shut down)
     */
    template<typename A, typename B>
    coro::awaitable<bool> sleep_for(std::chrono::duration<A,B> dur) {
        return sleep_until(std::chrono::system_clock::now()+dur);
    }

    ///retrieve associated IO context
    auto get_context() const {return _ctx;}

    ///Create timer
    /** 
     * @param ctx IO context
     */
    static Timer create(Context ctx) {
        Context::Handle h = ctx.create_timer();
        return Timer(std::move(ctx),h);
    }

protected:
    Context _ctx = {};
    Context::Handle _h = Context::null_handle;
};


}