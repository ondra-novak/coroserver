#pragma once

#include <deque>

namespace zerobus {


template<std::size_t action_size>
class ThreadDispatcher {
public:

    template<std::invocable<> Fn>
    void enqueue(Fn &&fn) {
        static_assert(sizeof(Fn) <= action_size);
        _queue.emplace_back(std::forward<Fn>(fn));
    }

    bool dispatch() {
        bool e = _queue.empty();
        if (e) return true;
        do {
            _lock = true;
            _queue.front().run();
            if (_lock) {
                _queue.pop_front();
                _lock = false;
            }
            e = _queue.empty();
        } while (!e);
        return false;
    }

    static ThreadDispatcher &get_instance() {
        static ThreadDispatcher inst;
        return inst;
    }

    bool empty() const {
        return _queue.empty();
    }

    std::size_t size() const {
        return _queue.size();
    }

protected:

    struct VTable {
        void (*call)(void *ctx);
        void (*destroy)(void *ctx);
    };

    template<typename T>
    static constexpr VTable vtable_def = {
            [](void *ctx){
                T *fn = reinterpret_cast<T *>(ctx);
                (*fn)();
            },
            [](void *ctx) {
                T *fn = reinterpret_cast<T *>(ctx);
                std::destroy_at(fn);
            }
    };

    class Action {
    public:

        template<std::invocable<> Fn>
        Action(Fn &&fn):_vtable(&vtable_def<Fn>) {
            static_assert(sizeof(Fn) <= action_size);
            Fn *t = reinterpret_cast<Fn *>(_space);
            std::construct_at(t, std::move(fn));
        }

        void run() {
            _vtable->call(_space);
        }

        ~Action() {
            _vtable->destroy(_space);
        }

        Action(const Action &) = delete;
        Action &operator=(const Action &) = delete;

    protected:
        const VTable *_vtable;
        char _space[action_size];
    };

    std::deque<Action> _queue;
    bool _lock = false;

};


}
