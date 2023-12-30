/*
 * socket_context.cpp
 *
 *  Created on: 2. 10. 2022
 *      Author: ondra
 */

#include "context.h"
#include "exceptions.h"
#include "ipoller.h"
#include "poller_epoll.h"

#include "socket_stream.h"

#include <system_error>
#include <coro.h>

#include "local_stream.h"
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

static void init_signals(__sighandler_t h) {
    for (int i: std::initializer_list<int>{SIGTERM, SIGINT, SIGHUP, SIGQUIT}) {
        signal(i, h);
    }

}


class ContextIOImpl: public IAsyncSupport, public std::enable_shared_from_this<ContextIOImpl> {
public:

    using AcceptResult = std::pair<SocketHandle, PeerName>;


    ContextIOImpl(coro::scheduler &sch);
    ContextIOImpl(std::unique_ptr<coro::scheduler> sch);
    ContextIOImpl(std::size_t iothreads);
    ~ContextIOImpl();

    virtual WaitResult io_wait(SocketHandle handle,
                     AsyncOperation op,
                     std::chrono::system_clock::time_point timeout) override;
    virtual void shutdown(SocketHandle handle)  override;
    virtual void close(SocketHandle handle)  override;

    void stop();

    coro::scheduler &get_scheduler()  {
        return *_scheduler;
    }

    Stream get_signal_stream();


protected:

    using SchDeleter = void (*)(coro::scheduler *sch);
    using SchPtr = std::unique_ptr<coro::scheduler, SchDeleter>;


    SchPtr _scheduler;
    std::unique_ptr<IPoller> _disp;
    coro::future<void> _disp_run;

    std::once_flag _signal_init;
    Stream _signal_stream;

};




void close_socket(const SocketHandle &handle) {
    ::close(handle);
}


ContextIOImpl::ContextIOImpl(coro::scheduler &sch)
        :_scheduler(SchPtr(&sch,[](coro::scheduler *){}))
        ,_disp(std::make_unique<Poller_epoll>())
        ,_disp_run(_disp->start(*_scheduler)) {
}

ContextIOImpl::ContextIOImpl(std::size_t iothreads)
        :_scheduler(SchPtr(new coro::scheduler(iothreads),[](coro::scheduler *sch){delete sch;}))
        ,_disp(std::make_unique<Poller_epoll>())
        ,_disp_run(_disp->start(*_scheduler)) {
}

ContextIOImpl::ContextIOImpl(std::unique_ptr<coro::scheduler> sch)
        :_scheduler(SchPtr(sch.release(),[](coro::scheduler *sch){delete sch;}))
        ,_disp(std::make_unique<Poller_epoll>())
        ,_disp_run(_disp->start(*_scheduler)) {
}

ContextIOImpl::~ContextIOImpl() {
    _disp->stop();
    bool has_signals = true;
    std::call_once(_signal_init, [&]{
        has_signals = false;;
    });
    if (!has_signals) {
        init_signals(SIG_DFL);
        _signal_stream = Stream();
    }
    _scheduler->await(_disp_run);
}

WaitResult ContextIOImpl::io_wait(SocketHandle handle,
                 AsyncOperation op,
                 std::chrono::system_clock::time_point timeout) {
    return _disp->io_wait(handle, op, timeout);

}
void ContextIOImpl::shutdown(SocketHandle handle) {
    _disp->shutdown(handle);

}
void ContextIOImpl::close(SocketHandle handle)   {
    _disp->close(handle);
    ::close(handle);
    /* c
    */
}

void ContextIOImpl::stop() {
    _disp->stop();

}


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
        auto wtres = socket.io_wait(AsyncOperation::accept,std::chrono::system_clock::time_point::max());
        bool connected = co_await wtres.has_value();
        if (!connected || !wtres.get()) break;
        sockaddr_storage addr;
        socklen_t slen = sizeof(addr);
        int s = ::accept4(socket, reinterpret_cast<sockaddr *>(&addr), &slen,
                SOCK_NONBLOCK|SOCK_CLOEXEC);
        if (s>=0) {
            co_yield Stream (SocketStream::create(
                            socket.set_socket_handle(s),
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
        auto dl = ctx.get_scheduler().sleep_for(std::chrono::seconds(delay_sec), &socket);
        //register stop callback, which will cancel our delay
        std::stop_callback stpcb(stop, [&]{ctx.get_scheduler().cancel(&socket);});
        //check stop requested finally, before wait, it would appear before registration
        if (stop.stop_requested()) {
            //if requested, cancel our wait
            ctx.get_scheduler().cancel(&socket);
        }
        //in all case, wait for delay, retrieve result
        try {
            co_await dl;
        } catch (const coro::broken_promise_exception &) {
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

        auto res = socket.io_wait(AsyncOperation::connect, TimeoutSettings::from_duration(timeout));

        co_await res.has_value();
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

coro::scheduler& Context::get_scheduler() {
           return _ptr->get_scheduler();
}

Context::Context(coro::scheduler &sch)
    :_ptr(std::make_shared<ContextIOImpl>(sch)) {}

Context::Context(std::unique_ptr<coro::scheduler> sch)
    :_ptr(std::make_shared<ContextIOImpl>(std::move(sch))) {}
Context::Context(unsigned int iothreads)
    :_ptr(std::make_shared<ContextIOImpl>(iothreads)) {}

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



static int signal_fd = -1;


static void signal_hndl(int sig) {
    std::ignore = ::write(signal_fd, &sig, sizeof(sig));
}



Stream Context::create_intr_listener() {
    return _ptr->get_signal_stream();

}

inline Stream ContextIOImpl::get_signal_stream() {
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

}

