#pragma once
#ifndef SRC_COROSERVER_ATOMIC_MUTEX_H_
#define SRC_COROSERVER_ATOMIC_MUTEX_H_

#include <coro.h>

namespace coroserver {


class AtomicMutex {
public:

    void lock() {
        while (_state.exchange(true, std::memory_order_acquire)) {
            _state.wait(true, std::memory_order_relaxed);
        }
    }

    void unlock() {
        _state.store(false, std::memory_order_release);
        _state.notify_one();
    }

    bool try_lock() {
        return !_state.exchange(true, std::memory_order_acquire);
    }

protected:
    waitable_atomic<bool> _state = {};
};


}




#endif /* SRC_COROSERVER_ATOMIC_MUTEX_H_ */
