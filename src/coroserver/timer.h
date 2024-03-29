#pragma once
#ifndef SRC_COROSERVER_TIMER_H_
#define SRC_COROSERVER_TIMER_H_

#include "async_support.h"
#include <coro.h>

namespace coroserver {


class Timer {
public:
    Timer(std::shared_ptr<IAsyncSupport> async_support)
        :_async_support(std::move(async_support)) {}

    ///sleep until
    /**
     * @param tp time when timer is triggered
     * @param ident optinal identification
     * @retval true time reached
     * @retval false timer canceled
     * @retval /canceled/ timer canceled because context is canceled
     */
    coro::future<bool> sleep_until(std::chrono::system_clock::time_point tp, const void *ident) {
        return _async_support->timer(tp, ident);
    }

    ///sleep for
    template<typename A, typename B>
    coro::future<bool> sleep_for(std::chrono::duration<A,B> dur, const void *ident) {
        return sleep_until(std::chrono::system_clock::now() + dur, ident);
    }

    ///Cancel time
    /**
     * @param ident identity of timer
     * @return object, which prevents timer creation while it is held.
     */
    auto cancel(const void *ident) {
        return _async_support->cancel_timer(ident);
    }


protected:
    std::shared_ptr<IAsyncSupport> _async_support;
};


}



#endif /* SRC_COROSERVER_TIMER_H_ */
