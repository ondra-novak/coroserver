/*
 * socket_context.cpp
 *
 *  Created on: 2. 10. 2022
 *      Author: ondra
 */

#include "exceptions.h"
#include "io_context.h"
#include "ipoller.h"
#include "poller_epoll.h"

#include "socket_stream.h"

#include <system_error>
#include <coro.h>



#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>
namespace coroserver {

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


AsyncSocket ContextIO::create_connected_socket(const PeerName &addr) {
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

AsyncSocket ContextIO::listen_socket(const PeerName &addr) {
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



coro::generator<Stream> ContextIO::accept(std::vector<PeerName> &list,
                                std::stop_token token, TimeoutSettings tms) {

    std::vector<coro::generator<Stream> > gens;
    std::vector<SocketHandle> handles;
    for (PeerName &x: list) {
        AsyncSocket socket = ContextIO::listen_socket(x);
        int id =x.get_group_id();
        x = PeerName::from_socket(socket,false).set_group_id(id);
        gens.push_back(listen_generator(std::move(socket), tms, token,  id));
    }
    return coro::aggregator(std::move(gens));
}

coro::generator<Stream> ContextIO::accept(std::vector<PeerName> &&list,
                                std::stop_token token, TimeoutSettings tms) {
    return accept(list,std::move(token),std::move(tms));
}


struct ConnectInfo {
    const PeerName &peer;
    std::optional<AsyncSocket> socket;
};

static coro::async<void> wait_connect(ContextIO ctx,
            const PeerName &peer,
            int delay_sec,
            int timeout,
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

coro::future<Stream> ContextIO::connect(std::vector<PeerName> list, int timeout_ms, TimeoutSettings tms) {
    //queue collects results for multiple sockets
    coro::queue<ConnectInfo> results;
    //stop source to stop futher waiting
    std::stop_source stop;
    std::size_t i;
    std::size_t cnt = list.size();
    //start coroutines, each for one peer
    for (i = 0; i < cnt; i++) {
        //coroutine is detached, because each put result to queue
        wait_connect(*this, list[i], i, timeout_ms, stop.get_token(), results).detach();
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

ContextIO ContextIO::create(coro::scheduler &sch) {
    return std::make_shared<ContextIOImpl>(sch);
}

ContextIO ContextIO::create(std::unique_ptr<coro::scheduler> sch) {
    return std::make_shared<ContextIOImpl>(std::move(sch));
}

ContextIO ContextIO::create(std::size_t iothreads) {
    return std::make_shared<ContextIOImpl>(iothreads);
}

void ContextIO::stop() {
   _ptr->stop();
}



}

