#pragma once
#ifndef SRC_COROSERVER_CORO_COMMON_H_
#define SRC_COROSERVER_CORO_COMMON_H_

#include <coro.h>

namespace coroserver {


template<typename T, typename Impl>
class awaitable: public coro::future<T> {
public:

    template<coro::hasnt_cast_operator<awaitable> ... Args>
    awaitable(Args && ... args): _content(std::forward<Args>(args)...) {
        coro::future<T>::operator <<([&]{return _content.initiate();});
    }

protected:
    Impl _content;
};



}



#endif /* SRC_COROSERVER_CORO_COMMON_H_ */
