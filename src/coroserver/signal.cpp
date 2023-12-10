#include "signal.h"
#include "socket_stream.h"
#include "pipe.h"
#include <csignal>
#include <unistd.h>
#include <fcntl.h>

namespace coroserver {

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

}
