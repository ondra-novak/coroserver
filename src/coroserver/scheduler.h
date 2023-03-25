/*
 * scheduler.h
 *
 *  Created on: 23. 2. 2023
 *      Author: ondra
 */

#ifndef SRC_USERVER_SCHEDULER_H_
#define SRC_USERVER_SCHEDULER_H_

namespace coroserver {

#include "ipoller.h"

class Scheduler {
public:

    using Promise = cocls::promise_with_default_v<WaitResult, WaitResult::closed>;

    ///timeout wait, use poller as scheduler
    void schedule(const void *ident, Promise p, std::chrono::system_clock::time_point timeout);

    ///cancels specified timer
    bool cancel_schedule(const void *ident);

    std::chrono::system_clock::time_point check_expired(std::chrono::system_clock::time_point now);

protected:

    struct SchItem {
        std::chrono::system_clock::time_point _tp;
        Promise _p;
        const void *_ident;
    };

    using SchVector = std::vector<SchItem>;
    SchVector _scheduled;

    static bool compare_item(const SchItem &a, const SchItem &b) {
        return a._tp > b._tp;
    }

    void pop_item();

};

inline void Scheduler::schedule(const void *ident, Promise p,
        std::chrono::system_clock::time_point timeout) {
    _scheduled.push_back({timeout, std::move(p), ident});
    std::push_heap(_scheduled.begin(), _scheduled.end(), compare_item);

}

inline bool Scheduler::cancel_schedule(const void *ident) {
    bool r = false;
    while (_scheduled.empty() && _scheduled[0]._ident == ident) {
        pop_item();
        r = true;
    }
    for (SchItem &item: _scheduled) {
        if (item._ident == ident) {
            auto p = std::move(item._p);
            p(WaitResult::complete);

        }
    }
    return r;
}

inline void Scheduler::pop_item() {
    std::pop_heap(_scheduled.begin(), _scheduled.end(), compare_item);
    _scheduled.pop_back();
}

inline std::chrono::system_clock::time_point Scheduler::check_expired(std::chrono::system_clock::time_point now) {
    while (!_scheduled.empty() && (_scheduled[0]._tp <= now || !_scheduled[0]._p)) {
        auto p = std::move(_scheduled[0]._p);
        pop_item();
        p(WaitResult::timeout);
    }
    if (_scheduled.empty()) return std::chrono::system_clock::time_point::max();
    else return _scheduled[0]._tp;
}


}



#endif /* SRC_USERVER_SCHEDULER_H_ */
