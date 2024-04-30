/*
 * socket_context.cpp
 *
 *  Created on: 2. 10. 2022
 *      Author: ondra
 */

#include "context.h"
#include "exceptions.h"
#include "epoll.h"

#include "socket_stream.h"

#include <system_error>
#include <coro.h>


#include "atomic_mutex.h"

#include <atomic>
#include <csignal>

#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/signalfd.h>
#include <thread>
#include <tuple>
namespace coroserver {

Context::Context()
        :_scheduler(CondVar{this}) {}

Context::Context(std::size_t iothreads)
        :_scheduler(CondVar{this})
{
    if (iothreads > 0) {
        if (iothreads > 1) {
            _tpool.emplace(iothreads-1);
            _scheduler.start([&](auto &&cb){
                _tpool->enqueue(std::move(cb));
            });
        } else {
            _scheduler.start([&](auto &&){});
        }
    }
}

void Context::CondVar::notify_all() {
    _owner->_engine.cancel_wait_for_next_event();
}
void Context::CondVar::wait_until(std::unique_lock<std::mutex> &lk, std::chrono::system_clock::time_point tp) {
    lk.unlock();
    auto ntf = _owner->_engine.wait_for_next_event(tp);
    if (_owner->_tpool.has_value()) {
        _owner->_tpool->enqueue(std::move(ntf));
    } else {
        ntf();
    }
    lk.lock();

}
void Context::CondVar::wait(std::unique_lock<std::mutex> &lk) {
    lk.unlock();
    auto ntf = _owner->_engine.wait_for_next_event();
    if (_owner->_tpool.has_value()) {
        _owner->_tpool->enqueue(std::move(ntf));
    } else {
        ntf();
    }
    lk.lock();

}


Context::~Context() {
    _engine.block_all(true);
    _scheduler.stop();
}

#if 0

static void init_signals(__sighandler_t h) {
    for (int i: std::initializer_list<int>{SIGTERM, SIGINT, SIGHUP, SIGQUIT}) {
        signal(i, h);
    }

}



class ContextIOImpl: public IAsyncSupport, public std::enable_shared_from_this<ContextIOImpl> {
public:

    using AcceptResult = std::pair<SocketHandle, PeerName>;
    using SchItem = Context::SchItem;

    ContextIOImpl() = default;
    ~ContextIOImpl();

    virtual coro::future<bool> input(SocketHandle handle, std::chrono::system_clock::time_point timeout) override;
    virtual coro::future<bool> output(SocketHandle handle, std::chrono::system_clock::time_point timeout) override;
    virtual void shutdown(SocketHandle handle)  override;
    virtual void close(SocketHandle handle)  override;
    virtual coro::future<bool> timer(std::chrono::system_clock::time_point tp, const void *ident) override;
    virtual TimerCancel cancel_timer(const void *ident) override;

    void run();
    void run(coro::function<void(SchItem)> schedule_fn);
    void start(coro::function<void(SchItem)> schedule_fn);
    void start(std::shared_ptr<coro::thread_pool> thread_pool);
    void start(unsigned int threads);

    void stop();


    Stream get_signal_stream();


protected:
    AsyncEPoll _epoll;
    std::stop_source _stp;
    std::vector<std::thread> _threads;


    std::once_flag _signal_init;
    Stream _signal_stream;

};




void close_socket(const SocketHandle &handle) {
    ::close(handle);
}



ContextIOImpl::~ContextIOImpl() {
    stop();
    bool has_signals = true;
    std::call_once(_signal_init, [&]{
        has_signals = false;;
    });
    if (!has_signals) {
        init_signals(SIG_DFL);
        _signal_stream = Stream();
    }
}

void ContextIOImpl::shutdown(SocketHandle handle) {
    _epoll.shutdown(handle);

}
void ContextIOImpl::close(SocketHandle handle)   {
    _epoll.unreg(handle);
    ::close(handle);
}

void ContextIOImpl::stop() {
    _stp.request_stop();
    for (auto &x: _threads) x.join();
    _threads.clear();

}

Context::Context():_ptr(std::make_shared<ContextIOImpl>()) {}

AsyncSocket Context::create_connected_socket(const PeerName &addr) {
    return addr.use_sockaddr([&](const sockaddr *saddr, socklen_t slen) {
        int sock = ::socket(saddr->sa_family, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, saddr->sa_family == AF_UNIX?0:IPPROTO_TCP);
        if (sock < 0) throw std::system_error(errno, std::system_category(), "::listen - create_socket");
        try {
            int flag = 1;
            if (saddr->sa_family == AF_INET || saddr->sa_family == AF_INET6) {
                if (::setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char *>(&flag),sizeof(int)))
                    throw std::system_error(errno, std::system_category(), "setsockopt(TCP_NODELAY)");
            }
            if (::connect(sock,saddr, slen)) {
                int err = errno;
                if (err != EWOULDBLOCK &&  err != EINPROGRESS && err != EAGAIN) {
                    throw std::system_error(errno, std::system_category(), "connect");
                }
            }
            return AsyncSocket(sock, _ptr);
        } catch (...) {
            ::close(sock);
            throw;
        }
    });

}

AsyncSocket Context::listen_socket(const PeerName &addr) {
    return addr.use_sockaddr([&](const sockaddr *saddr, socklen_t slen) {
        int sock = ::socket(saddr->sa_family, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, saddr->sa_family == AF_UNIX?0:IPPROTO_TCP);
        if (sock < 0) throw std::system_error(errno, std::system_category(), "::listen - create_socket");
        try {
            int flag = 1;
            if (saddr->sa_family == AF_INET6) {
                if (::setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char *>(&flag), sizeof(int)))
                    throw std::system_error(errno, std::system_category(), "setsockopt(IPV6_V6ONLY)");
            }
            if (saddr->sa_family == AF_INET || saddr->sa_family == AF_INET6) {
                if (::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&flag), sizeof(int)))
                    throw std::system_error(errno, std::system_category(), "setsockopt(SO_REUSEADDR)");
                if (::setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char *>(&flag),sizeof(int)))
                    throw std::system_error(errno, std::system_category(), "setsockopt(TCP_NODELAY)");
            }
            if (::bind(sock,saddr, slen))
                throw std::system_error(errno, std::system_category(), "bind");
            if (::listen(sock, SOMAXCONN))
                throw std::system_error(errno, std::system_category(), "listen");
            return AsyncSocket(sock, _ptr);
        } catch (...) {
            ::close(sock);
            throw;
        }
    });

}

static coro::generator<Stream> listen_generator(AsyncSocket socket,
        TimeoutSettings tmcfg,
        std::stop_token stoken,
        int group_id) {

    std::stop_callback stopcb(stoken, [&]{
        socket.shutdown();
    });

    while (true) {
        auto wtres = socket.input(std::chrono::system_clock::time_point::max());
        bool connected = co_await !!wtres;
        if (!connected || !wtres.get()) break;
        sockaddr_storage addr;
        socklen_t slen = sizeof(addr);
        int s = ::accept4(socket, reinterpret_cast<sockaddr *>(&addr), &slen,
                SOCK_NONBLOCK|SOCK_CLOEXEC);
        if (s>=0) {
            co_yield Stream (SocketStream::create(
                            socket.set_handle(s),
                            PeerName::from_sockaddr(&addr).set_group_id(group_id),
                            tmcfg));
        } else {

            int e = errno;
            if (e != EINTR) {
                throw std::system_error(errno, std::system_category(), "::accept4");
            }
        }

    }
}



coro::generator<Stream> Context::accept(std::vector<PeerName> &list,
                                std::stop_token token, TimeoutSettings tms) {

    std::vector<coro::generator<Stream> > gens;
    std::vector<SocketHandle> handles;
    for (PeerName &x: list) {
        AsyncSocket socket = Context::listen_socket(x);
        int id =x.get_group_id();
        x = PeerName::from_socket(socket,false).set_group_id(id);
        gens.push_back(listen_generator(std::move(socket), tms, token,  id));
    }
    return coro::aggregator(std::move(gens));
}

coro::generator<Stream> Context::accept(std::vector<PeerName> &&list,
                                std::stop_token token, TimeoutSettings tms) {
    return accept(list,std::move(token),std::move(tms));
}


struct ConnectInfo {
    const PeerName &peer;
    std::optional<AsyncSocket> socket;
};

static coro::async<void> wait_connect(Context &ctx,
            const PeerName &peer,
            int delay_sec,
            TimeoutSettings::Dur timeout,
            std::stop_token stop,
            coro::queue<ConnectInfo> &result) {

    AsyncSocket socket;

    //if delay_sec is nonzero - add some delay
    if (delay_sec) {
        //wait for delay - store future - to solve race condition
        auto dl = ctx.create_timer().sleep_for(std::chrono::seconds(delay_sec), &socket);
        //register stop callback, which will cancel our delay
        std::stop_callback stpcb(stop, [&]{ctx.create_timer().cancel(&socket);});
        //check stop requested finally, before wait, it would appear before registration
        if (stop.stop_requested()) {
            //if requested, cancel our wait
            ctx.create_timer().cancel(&socket);
        }
        //in all case, wait for delay, retrieve result
        bool canceled = co_await !dl;
        if (canceled) {
            result.push(ConnectInfo{peer, {}});
            co_return;
        }
    }
    try {
        //create socket
        socket = ctx.create_connected_socket(peer);
    } catch (...) {
        //failed to create socket - report failure
        result.push(ConnectInfo{peer, {}});
        //exit
        co_return;
    }

    {
        //in this state, we can use socket handle to mark it closing when stop is requested
        std::stop_callback stpcb(stop, [&]{socket.shutdown();});

        auto res = socket.output(TimeoutSettings::from_duration(timeout));

        co_await res.wait();
    }

    if (stop.stop_requested()) {
        //failed to create socket - report failure
        result.push(ConnectInfo{peer, {}});
        //exit
        co_return;
    }

    int error_code;

    socklen_t error_code_size = sizeof(error_code);

    ::getsockopt(socket, SOL_SOCKET, SO_ERROR, &error_code, &error_code_size);

    if (error_code) {
        //failed to create socket - report failure
        result.push(ConnectInfo{peer, {}});
        //exit
        co_return;
    }


    result.push(ConnectInfo{peer, std::move(socket)});
}

coro::future<Stream> Context::connect(std::vector<PeerName> list, TimeoutSettings::Dur connect_timeout, TimeoutSettings tms) {
    //queue collects results for multiple sockets
    coro::queue<ConnectInfo> results;
    //stop source to stop futher waiting
    std::stop_source stop;
    std::size_t i;
    std::size_t cnt = list.size();
    //start coroutines, each for one peer
    for (i = 0; i < cnt; i++) {
        //coroutine is detached, because each put result to queue
        wait_connect(*this, list[i], i, connect_timeout, stop.get_token(), results).detach();
    }
    //contains connected stream
    std::optional<Stream> connected;
    //even if we wait for first incomming connection, we must wait for all coroutines
    for (i = 0; i < cnt; i++) {
        //retrieve result from queue
        ConnectInfo nfo = std::move(co_await results.pop());
        //if socket is set, then connection was successful
        if (nfo.socket.has_value()) {
            //but if we don't have stream
            if (!connected.has_value()) {
                //create it now
                connected = SocketStream::create(std::move(*nfo.socket), nfo.peer, tms);
                //and stop other attempts
                stop.request_stop();
            }
        }
    }
    //finally if we retrieved no connection, report exception
    if (!connected.has_value()) throw ConnectFailedException();
    //otherwise, return stream
    co_return std::move(*connected);
}


void Context::stop() {
   if (_ptr) {
       _ptr->stop();
       _ptr.reset();
   }
}

Stream Context::create_pipe(TimeoutSettings tms) {
    int fds[2];
    int r = pipe2(fds, O_CLOEXEC | O_NONBLOCK);
    if (r < 0) throw std::system_error(errno, std::system_category());
    return Stream(std::make_shared<LocalStream>(
            AsyncSocket(fds[0], _ptr),
            AsyncSocket(fds[1], _ptr),
            PeerName(),
            tms));
}

Stream Context::create_stdio(TimeoutSettings tms) {
    int rdfd = fcntl(0, F_DUPFD_CLOEXEC, 0);
    if (rdfd < 0) {
        int e =errno;
        throw std::system_error(e, std::system_category(), "failed to dup stdin");
    }
    int wrfd = fcntl(0, F_DUPFD_CLOEXEC, 1);
    if (wrfd < 0) {
        int e = errno;
        ::close(rdfd);
        throw std::system_error(e, std::system_category(), "failed to dup stdout");
    }
    return Stream(std::make_shared<LocalStream>(
            AsyncSocket(rdfd, _ptr),
            AsyncSocket(wrfd, _ptr),
            PeerName(),
            tms));

}

Stream Context::read_named_pipe(const std::string &name, TimeoutSettings tms) {
    int fd = ::open(name.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)  {
        int e =errno;
        throw std::system_error(e, std::system_category(), "failed to open named pipe: " + name);
    }
    return Stream(std::make_shared<LocalStream>(
            AsyncSocket(fd, _ptr),
            AsyncSocket(),
            PeerName(),
            tms));
}


Context::~Context() {
    stop();
}

Context::Context(Context &&other)
    :_ptr(std::move(other._ptr)) {}

Context& Context::operator =(Context &&other) {
    if (this != &other) {
        stop();
        _ptr = std::move(other._ptr);
    }
    return *this;

}

Stream Context::write_named_pipe(const std::string &name, TimeoutSettings tms) {
    int fd = ::open(name.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)  {
        int e =errno;
        throw std::system_error(e, std::system_category(), "failed to open named pipe: " + name);
    }
    return Stream(std::make_shared<LocalStream>(
            AsyncSocket(),
            AsyncSocket(fd, _ptr),
            PeerName(),
            tms));
}

Timer Context::create_timer() {
    return Timer(_ptr);
}


static int signal_fd = -1;


static void signal_hndl(int sig) {
    std::ignore = ::write(signal_fd, &sig, sizeof(sig));
}



Stream Context::create_intr_listener() {
    return _ptr->get_signal_stream();

}

void ContextIOImpl::run() {
    _epoll.serve([](auto){}, _stp.get_token());
}

void ContextIOImpl::run(coro::function<void(SchItem)> schedule_fn) {
    _epoll.serve(std::move(schedule_fn), _stp.get_token());
}

void ContextIOImpl::start(coro::function<void(SchItem)> schedule_fn) {
    _threads.emplace_back([this,schedule_fn = std::move(schedule_fn)]() mutable {
        run(std::move(schedule_fn));
    });
}

void ContextIOImpl::start(std::shared_ptr<coro::thread_pool> thread_pool) {
    start([thread_pool](SchItem x){
        thread_pool->enqueue(std::move(x));
    });
}

void ContextIOImpl::start(unsigned int threads) {
    start(std::make_shared<coro::thread_pool>(threads));
}

inline coro::future<bool> ContextIOImpl::input(SocketHandle handle,
        std::chrono::system_clock::time_point timeout) {
    return _epoll.wait(handle, Operation::input, timeout);
}

inline coro::future<bool> ContextIOImpl::output(SocketHandle handle,
        std::chrono::system_clock::time_point timeout) {
    return _epoll.wait(handle, Operation::output, timeout);
}

inline coro::future<bool> ContextIOImpl::timer(
        std::chrono::system_clock::time_point tp, const void *ident) {
    return _epoll.wait(tp, ident);
}

inline ContextIOImpl::TimerCancel ContextIOImpl::cancel_timer(const void *ident) {
    return TimerCancel(_epoll.cancel(ident));
}

Stream ContextIOImpl::get_signal_stream() {
    std::call_once(_signal_init, [&]{
        int fds[2];
        if (pipe2(fds,O_CLOEXEC|O_NONBLOCK) < 0)
            throw std::system_error(errno, std::system_category());

        signal_fd = fds[1];
        init_signals(signal_hndl);
        _signal_stream = Stream(std::make_shared<LocalStream>(
                AsyncSocket(fds[0],shared_from_this()),
                AsyncSocket(fds[1],shared_from_this()),
                PeerName(),TimeoutSettings{}));
    });
    return _signal_stream;
}

void Context::run() {
    _ptr->run();
}

void Context::run(coro::function<void(SchItem)> schedule_fn) {
    _ptr->run(std::move(schedule_fn));
}
void Context::start() {
    _ptr->start([](auto){});
}

void Context::start(coro::function<void(SchItem)> schedule_fn) {
    _ptr->start(std::move(schedule_fn));
}

void Context::start(std::shared_ptr<coro::thread_pool> thread_pool) {
    _ptr->start(std::move(thread_pool));
}

void Context::start(unsigned int threads) {
    _ptr->start(threads);
}



std::pair<Stream, Stream> Context::create_pair(TimeoutSettings tms) {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM |SOCK_NONBLOCK|SOCK_CLOEXEC, 0, sockets)<0) {
        throw std::system_error(errno, std::system_category(), "Failed to create socketpair");
    }
    return {
        Stream(SocketStream::create(AsyncSocket(sockets[0],_ptr), PeerName(), tms)),
        Stream(SocketStream::create(AsyncSocket(sockets[1],_ptr), PeerName(), tms))
    };
}


#endif

}

