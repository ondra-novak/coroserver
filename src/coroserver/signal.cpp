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


SignalHandler::SignalHandler(ContextIO ioctx)
    :_signal_stream(PipeStream::create(ioctx, StaticSignalState::get_instance().signal_read, -1))
    ,_awt(*this){

    _awt << [&]{return _signal_stream.read();};
}

SignalHandler::~SignalHandler() {
    _signal_stream.shutdown();
}

cocls::future<void> SignalHandler::operator()(int signal) {
    return [this,signal](auto promise) {
        std::lock_guard _(_mx);
        Promises &p = _listeners[signal];
        bool create_signal = p.empty();
        p.push_back(std::move(promise));
        if (create_signal) ::signal(signal, &signal_handler);
    };
}

cocls::suspend_point<void> SignalHandler::on_signal(cocls::future<std::string_view> &f) noexcept {
    cocls::suspend_point<void> out;
    try {
        std::lock_guard _(_mx);
        std::string_view data = *f;
        if (!data.empty()) {
            for (auto sig: data) {
                auto iter = _listeners.find(sig);
                if (iter != _listeners.end()) {
                    for (auto &promise: iter->second) {
                        out << promise();
                    }
                    _listeners.erase(iter);
                }
                signal(sig, SIG_DFL);
            }
            _awt << [&]{return _signal_stream.read();};
        }

    } catch (...) {
        //ignore exceptions
    }
    return out;
}

}
