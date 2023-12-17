#include "signal.h"
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <bitset>

namespace coroserver {




class LockfreeSignaling {
public:

    LockfreeSignaling() {
        _thr = std::thread([&]{worker();});
    }
    ~LockfreeSignaling() {
        _exit = true;
        ++_trigger;
        _trigger.notify_all();
        _thr.join();

        for (unsigned int i = 0; i < _installed.size();++i) {
            if (_installed.test(i)) {
                clear_signal(i);
            }
        }
    }

    coro::future<unsigned int> reg_signals(std::initializer_list<int> list, HandlerID id) {
        std::bitset<NSIG> mask;
        mask.reset();
        for (unsigned int i: list) mask.set(i);
        return [&](auto prom){
            std::lock_guard _(_mx);
            _regs.push_back({std::move(prom), id, mask});
        };
    }

    auto cancel(HandlerID id) {
        std::lock_guard lk(_mx);
        coro::future<void>::pending_notify ntf;
        auto iter = std::find_if(_regs.begin(), _regs.end(), [&](const Reg &reg) {
            return (reg.id == id);
        });
        coro::promise<unsigned int> prom;
        if (iter != _regs.end()) {
            prom = std::move(iter->prom);
            _regs.erase(iter);
        }
        return prom.drop();
    }


protected:

    struct Reg {
        coro::promise<unsigned int> prom;
        HandlerID id;
        std::bitset<NSIG> mask;
    };

    static constexpr unsigned int sig_hash_size = 11;
    static std::atomic<int> _sig_reported[sig_hash_size];
    static waitable_atomic<int> _trigger;
    bool _exit = false;
    std::bitset<NSIG> _installed;
    std::thread _thr;
    std::mutex _mx;
    std::vector<Reg> _regs;


    static void set_signal(int sig, __sighandler_t hndl) {
        struct sigaction sa;
        sa.sa_handler = hndl;
        sa.sa_flags = 0;
        sigemptyset(&sa.sa_mask);

        if (sigaction(sig, &sa, NULL)) {
            throw std::system_error(errno, std::system_category());
        }
    }

    static void clear_signal(int sig) {
        set_signal(sig, SIG_DFL);
    }


    void install(unsigned int sig) {
        if (_installed.test(sig)) return;
        _installed.set(sig);
        set_signal(sig,&handler);
    }

    void worker() {
        int oldtrig = 0;
        std::vector<coro::future<unsigned int>::pending_notify> ntfs;
        do {
            ntfs.clear();
            _trigger.wait(oldtrig);
            oldtrig = _trigger.load(std::memory_order_relaxed);
            std::lock_guard lk(_mx);
            for (auto &x: _sig_reported) {
                int s = x.load(std::memory_order_relaxed);
                if (s) {
                    auto iter = std::remove_if(_regs.begin(), _regs.end(), [&](Reg &r){
                        if (r.mask.test(s)) {
                            ntfs.push_back(r.prom(s));
                            return true;
                        }
                        return false;
                    });
                    _regs.erase(iter,_regs.end());
                }
                x.store(0, std::memory_order_relaxed);
            }
        } while (!_exit);
        ntfs.clear();
    }


    static void handler(int signum) {
        for (unsigned int i = 0; i < sig_hash_size; ++i) {
            int need = 0;
            unsigned int idx = (signum + i) % sig_hash_size;
            if (_sig_reported[idx].compare_exchange_strong(need, signum) || need == signum) break;
        }
       ++_trigger;
       _trigger.notify_all();
    }
};

std::atomic<int> LockfreeSignaling::_sig_reported[sig_hash_size] = {};
waitable_atomic<int> LockfreeSignaling::_trigger = {};

static LockfreeSignaling &signaler() {
    static LockfreeSignaling inst;
    return inst;
}

coro::future<unsigned int> signal(int signum, HandlerID id) {
    signal({signum}, id);
}

coro::future<unsigned int> signal(std::initializer_list<int> signums, HandlerID id) {
    return signaler().reg_signals(signums, id);
}

coro::future<unsigned int>::pending_notify cancel_signal_await(HandlerID id) {
    return signaler().cancel(id);
}




#if 0

struct StaticSignalState {
    int signal_read;
    int signal_write;

    StaticSignalState() {
        int p[2];
        int e = pipe2(p,O_CLOEXEC| O_NONBLOCK);
        if (e) {
            throw std::system_error(errno, std::system_category(), "StaticSignalState::pipe2");
        }
        signal_read = p[0];
        signal_write = p[1];
    }

    ~StaticSignalState() {
        ::close(signal_read);
        ::close(signal_write);
    }

    static StaticSignalState &get_instance() {
        static StaticSignalState state;
        return state;
    }

};

static void signal_handler(int s) {
    char sig = static_cast<char>(s);
    auto &st = StaticSignalState::get_instance();
    ::write(st.signal_write, &sig, 1);
    signal(s, SIG_DFL);
}


SignalHandler::SignalHandler(AsyncSupport ioctx)
    :_signal_stream(PipeStream::create(ioctx, dup(StaticSignalState::get_instance().signal_read), -1))
    ,_awt(*this){

    _awt << [&]{return _signal_stream.read();};
}

SignalHandler::~SignalHandler() {
    _signal_stream.shutdown();
}

coro::future<void> SignalHandler::operator()(std::initializer_list<int > signals) {
    return [this,signals](auto promise) {
        std::lock_guard _(_mx);
        auto shp = std::make_shared<coro::promise<void> >(std::move(promise));
        for (auto sig: signals) {
            Promises &p = _listeners[sig];
            p.erase(std::remove_if(p.begin(), p.end(), [](const auto &x){return !(*x);}),p.end());
            bool create_signal = p.empty();
            p.push_back(shp);
            if (create_signal) ::signal(sig, &signal_handler);
        }
    };

}

coro::future<void> SignalHandler::operator()(int signal) {
    return operator()({signal});
}

coro::suspend_point<void> SignalHandler::on_signal(coro::future<std::string_view> &f) noexcept {
    std::lock_guard _(_mx);
    try {
        std::string_view data = *f;
        if (!data.empty()) {
            coro::suspend_point<void> out;
            for (auto sig: data) {
                auto iter = _listeners.find(sig);
                if (iter != _listeners.end()) {
                    for (auto &promise: iter->second) {
                        out << (*promise)();
                    }
                    _listeners.erase(iter);
                }
                signal(sig, SIG_DFL);
            }
            _awt << [&]{return _signal_stream.read();};
            return out;
        }

    } catch (...) {
        //
    }
    return coro::coro_queue::create_suspend_point([&]{
        _listeners.clear();
    });
}

#endif

}
