#include "socket_context.h"
#include "epoll.h"

#include <netinet/in.h>
#include <sys/socket.h>
namespace coroserver {


class Socket : public SocketBase {
public:

    using SocketBase::SocketBase;

    Socket(Socket &&other):SocketBase(std::move(other)) {}
    Socket(SocketBase &&other):SocketBase(std::move(other)) {}


    coro::future<std::string_view> recv(std::chrono::system_clock::time_point timeout);
    coro::future<bool> send(const std::string_view &data, std::chrono::system_clock::time_point timeout) ;
    void close_output();

    bool is_read_timeout() {return _timeout;}



protected:
    coro::future<bool> _op_input;
    coro::future<bool> _op_output;
    bool _timeout = false;

    std::vector<char> _input_buffer;
    std::size_t _input_buffer_allocate = 4096;
    coro::promise<std::string_view> _recv_prom;
    std::chrono::system_clock::time_point _recv_timeout;

    std::string_view _output_buffer;
    coro::promise<bool> _send_prom;
    std::chrono::system_clock::time_point _send_timeout;

    coro::promise<std::string_view>::notify do_recv();
    coro::promise<bool>::notify do_send();





};


class ListeningSocket: public SocketBase {
public:
    using SocketBase::SocketBase;


    ListeningSocket (ListeningSocket &&other):SocketBase(std::move(other)) {}

    coro::future<Stream> accept(std::chrono::system_clock::time_point timeout);


protected:
    coro::future<bool> _op_input;
    coro::promise<Stream> _accept_prom;
    std::chrono::system_clock::time_point _accept_timeout;

    coro::promise<SocketBase>::notify do_accept();
};


class SocketStream: public AbstractStreamWithMetadata {
public:


protected:
    Socket _sock;
};

SocketBase::SocketBase(Ident s, PeerName peer_name, std::weak_ptr<ISocketContext> context)
    :_s(std::move(s))
    ,_peer_name(peer_name)
    ,_context(std::move(context)) {}

SocketBase& SocketBase::operator =(SocketBase &&other) {
    if (&other != this) {
        std::destroy_at(this);
        std::construct_at(this,std::move(other));
    }
    return *this;
}

SocketBase::~SocketBase() {
    auto lk = _context.lock();
    if (lk) lk->close(_s);
}

void SocketBase::shutdown() {
    auto lk = _context.lock();
    if (lk) lk->shutdown(_s);
}

class SocketContextImpl: public ISocketContext, public std::enable_shared_from_this<SocketContextImpl> {
public:

    AsyncEPoll _epoll;

    virtual Server listen(std::vector<PeerName> &addresses,
                          TimeoutSettings stream_timeouts = defaultTimeout) override;

    virtual coro::future<Stream> connect(const std::vector<PeerName> &addresses,
                TimeoutSettings::Dur connect_timeout = defaultConnectTimeout,
                TimeoutSettings stream_timeouts = defaultTimeout) override;
    virtual coro::future<Stream> connect(const PeerName &address,
                TimeoutSettings::Dur connect_timeout = defaultConnectTimeout,
                TimeoutSettings stream_timeouts = defaultTimeout) override;


    virtual ListeningSocket listen(const PeerName &addr);
    virtual void serve(coro::function<void(SchItem)> schedule_fn) override;
    virtual void start_thread(coro::function<void(ISocketContext::SchItem)> schedule_fn) override;
    virtual void shutdown(Ident ident) override;
    virtual void close(Ident ident) override;
    virtual void stop() override;

protected:

    std::vector<std::thread> _threads;
    std::stop_source _stpsrc;



};

coro::future<std::string_view> Socket::recv(std::chrono::system_clock::time_point timeout) {
    return [&](auto promise) {
        _recv_prom = std::move(promise);
        _recv_timeout = timeout;
        _timeout = false;
        _input_buffer.resize(_input_buffer_allocate);
        do_recv();
    };
}

coro::future<bool> Socket::send(const std::string_view &data, std::chrono::system_clock::time_point timeout) {
    return [&](auto promise) {
        _send_prom = std::move(promise);
        _send_timeout = timeout;
        _output_buffer = data;
        do_send();
    };
}

coro::promise<std::string_view>::notify Socket::do_recv() {
    int r = ::recv(_s, _input_buffer.data(), _input_buffer.size(), MSG_NOSIGNAL|MSG_DONTWAIT);
    if (r >= 0) {
        if (static_cast<std::size_t>(r) == _input_buffer.size()) {
            _input_buffer_allocate = _input_buffer_allocate * 3/2;
        }
        return _recv_prom(_input_buffer.data(), r);
    } else {
        int e = errno;
        switch (e) {
            case EWOULDBLOCK: {
                    auto lk = _context.lock();
                    if (lk) {
                        auto ctx = static_cast<SocketContextImpl *>(lk.get());
                        _op_input << [&]{return ctx->_epoll.wait(_s, Operation::input, _recv_timeout);};
                        _op_input >> [this] {
                            if (_op_input.has_value()) {
                                bool res = _op_input;
                                if (res) {
                                    return do_recv();
                                } else {
                                    _timeout = true;
                                    return _recv_prom();
                                }
                            } else {
                                return _recv_prom();
                            }
                        };
                        return {};
                    } else {
                        return _recv_prom();
                    }
                }
                break;
            case EPIPE:
                return _recv_prom();
            default:
                return _recv_prom.reject(std::system_error(e, std::system_category(), "recv error"));
        }
    }
}

coro::promise<bool>::notify Socket::do_send() {
    if (_output_buffer.empty()) {
        return _send_prom(true);
    }
    int r = ::send(_s, _output_buffer.data(), _output_buffer.size(), MSG_NOSIGNAL|MSG_DONTWAIT);
    if (r > 0) {
        _output_buffer = _output_buffer.substr(r);
        return do_send();
    }
    if (r == 0) {
        return _send_prom(false);
    }
    int e = errno;
    switch (e) {
        case EWOULDBLOCK: {
                auto lk = _context.lock();
                if (lk) {
                    auto ctx = static_cast<SocketContextImpl *>(lk.get());
                    _op_output <<[&]{return ctx->_epoll.wait(_s, Operation::output, _send_timeout);};
                    _op_output >>[this]{
                        if (_op_output.has_value()) {
                            bool res = _op_output;
                            if (res) {
                                return do_send();
                            } else {
                                return _send_prom(false);
                            }
                        } else {
                            return _send_prom(false);
                        }
                    };
                    return {};
                } else {
                    return _send_prom(false);
                }
            }break;
        case EPIPE:
            return _send_prom(false);
        default:
            return _send_prom.reject(std::system_error(e, std::system_category(), "send error"));
    }
}

coro::future<Stream> ListeningSocket::accept(std::chrono::system_clock::time_point timeout) {
    return [&](auto promise){
        _accept_prom = std::move(promise);
        _accept_timeout = timeout;
        do_accept();
    };
}

coro::promise<SocketBase>::notify ListeningSocket::do_accept() {
    sockaddr_storage stor;
    sockaddr *saddr = reinterpret_cast<sockaddr *>(&stor);
    socklen_t slen = sizeof(stor);

    int r = ::accept4(_s, saddr, &slen, SOCK_CLOEXEC|SOCK_NONBLOCK);
    if (r>=0) {
        return _accept_prom(r, PeerName::from_sockaddr(saddr), _context);
    }
    int e = errno;
    switch (e) {
        case EWOULDBLOCK: {
            auto lk = _context.lock();
            if (lk) {
                auto ctx = static_cast<SocketContextImpl *>(lk.get());
                _op_input <<[&]{return ctx->_epoll.wait(_s, Operation::input, _accept_timeout);};
                _op_input >>[this]{
                    if (_op_input.has_value()) {
                        bool res = _op_input;
                        if (res) {
                            return do_accept();
                        } else {
                            return _accept_prom.reject(std::system_error(ETIMEDOUT, std::system_category(), "accept timeout"));
                        }
                    } else {
                        return _accept_prom.cancel();
                    }
                };
                return {};
            } else{
                return _accept_prom.cancel();
            }
        } break;
        case EPIPE:
            return _accept_prom.cancel();
        default:
            return _accept_prom.reject(std::system_error(e, std::system_category(), "accept error"));
    }
}




inline FileDescriptor createStreamSocket(int domain) {
    int protocol = (domain == AF_INET|| domain == AF_INET6)?IPPROTO_TCP:0;
    FileDescriptor fd = ::socket(domain, SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK, protocol);
    if (!fd) throw std::system_error(errno, std::system_category(), "socket creation failed");
    return fd;
}


coro::future<SocketBase> ConnectingSocket::connect() {
    static constexpr auto finalize = [](FileDescriptor &&sock, coro::promise<SocketBase> &&promise, const PeerName &target, const std::weak_ptr<ISocketContext> &context){
            int err;
            socklen_t errlen = sizeof(err);
            int ret = getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &errlen);
            if (ret < 0) err = errno;
            if (err) promise.reject(std::system_error(err, std::system_category(), "Connect failed"));
            promise(sock.release(), target, context);
    };

    return [&](auto promise) {
        sockaddr_storage stor;
        sockaddr *saddr = reinterpret_cast<sockaddr *>(&stor);
        socklen_t slen = _target.to_sockaddr(saddr, sizeof(stor));
        auto sock = createStreamSocket(saddr->sa_family);
        int r = ::connect(sock,saddr,slen);
        if (r < 0) {
            int e = errno;
            switch (e) {
                case EWOULDBLOCK: {
                    auto lk = _context.lock();
                    if (lk) {
                        auto ctx = static_cast<SocketContextImpl *>(lk.get());
                        _wait_fut << [&]{return ctx->_epoll.wait(sock, Operation::output, _timeout);};
                        _wait_fut >> [this,promise = std::move(promise), sock = std::move(sock)]()mutable{
                                if (_wait_fut.has_value()){
                                    bool st = _wait_fut;
                                    if (st) {
                                        finalize(std::move(sock), std::move(promise), _target, _context);
                                    } else {
                                        promise.reject(std::system_error(ETIMEDOUT, std::system_category(), "Connect failed"));
                                    }
                                } else {
                                    promise.cancel();
                                }
                        };
                    } else {
                        promise.cancel();
                    }
                } break;
                default:
                    promise.reject(std::system_error(e, std::system_category(), "Connect failed"));
                    break;
            }
        } else {
            finalize(std::move(sock), std::move(promise), _target, _context);
        }
    };

}
ListeningSocket SocketContextImpl::listen(const PeerName &addr) {
    sockaddr_storage stor;
    sockaddr *saddr = reinterpret_cast<sockaddr *>(&stor);
    socklen_t slen = addr.to_sockaddr(saddr, sizeof(stor));
    auto sock = createStreamSocket(saddr->sa_family);
    if (::bind(sock, saddr, slen)) throw std::system_error(errno,std::system_category(), "Cannot bind socket to address");
    if (::listen(sock, SOMAXCONN)) throw std::system_error(errno,std::system_category(), "Socket 'listen' failed");
    slen = sizeof(stor);
    if (::getsockname(sock,saddr,&slen)) throw std::system_error(errno,std::system_category(), "getsockname failed");
    return ListeningSocket(sock.release(), PeerName::from_sockaddr(saddr),weak_from_this());

}


void Socket::close_output() {
    ::shutdown(_s, SHUT_WR);
}

inline void SocketContextImpl::serve(coro::function<void(SchItem)> schedule_fn) {
    _epoll.serve(std::move(schedule_fn), _stpsrc.get_token());
}

inline void SocketContextImpl::shutdown(Ident ident) {
    _epoll.shutdown(ident);
}

inline void SocketContextImpl::close(Ident ident) {
    _epoll.unreg(ident);
    ::close(ident);
}

Context::Context():_ctx(std::make_shared<SocketContextImpl>()) {}

Context::~Context() {
    if (_ctx) _ctx->stop();
}

void Context::start_single_thread() {
    if (_ctx) _ctx->serve([](auto){});
}

void Context::start_thread(coro::function<void(ISocketContext::SchItem)> schedule_fn) {
    if (_ctx) _ctx->start_thread(std::move(schedule_fn));
}

inline void SocketContextImpl::start_thread(coro::function<void(ISocketContext::SchItem)> schedule_fn) {
    _threads.emplace_back([this, schedule_fn = std::move(schedule_fn)]()mutable{
        _epoll.serve(std::move(schedule_fn), _stpsrc.get_token());
    });
}

void SocketContextImpl::stop() {
    _stpsrc.request_stop();
    for (auto &x:_threads) x.join();
    _threads.clear();
}

static coro::generator<Stream> listen_generator(ListeningSocket sck, TimeoutSettings tms, std::stop_token stp, ) {
    std::stop_callback _(stp,[&]{
       sck.shutdown();
    });
    try {
        while (true) {
            SocketBase newsck = co_await sck.accept(std::chrono::system_clock::time_point::max());
            co_yield std::move(newsck);
        }


    } catch (const coro::await_canceled_exception &) {
        //empty
    }
}

Context::Server Context::listen(std::vector<PeerName> &addresses) {
    std::vector<coro::generator<SocketBase> > gens;
    std::stop_source stp;
    for (auto &a: addresses) {
        ListeningSocket sck = listen(a);
        a = sck.get_peer_name();
        gens.push_back(listen_generator(std::move(sck), stp.get_token()));
    }
    return Server(coro::aggregator(std::move(gens)), std::move(stp));
}

virtual Server SocketContextImpl::listen(std::vector<PeerName> &addresses,
                      TimeoutSettings stream_timeouts = defaultTimeout)  {

}


}

