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
#include <cocls/generator_aggregator.h>


#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>
namespace coroserver {


ContextIOImpl::ContextIOImpl(std::shared_ptr<cocls::thread_pool> pool)
        :_pool(pool)
        ,_disp(std::make_unique<Poller_epoll>(*pool)) {

}


ContextIOImpl::ContextIOImpl(std::size_t iothreads)
    :ContextIOImpl(std::make_shared<cocls::thread_pool>(iothreads))
{

}

ContextIOImpl::~ContextIOImpl() {
    stop();
}


void ContextIOImpl::close(SocketHandle h) {
    _disp->handle_closed(h);
    ::close(h);
}

SocketHandle ContextIO::create_connected_socket(const PeerName &addr) {
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
            return sock;
        } catch (...) {
            ::close(sock);
            throw;
        }
    });

}

ListeningSocketHandle ContextIO::listen_socket(const PeerName &addr) {
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
            return sock;
        } catch (...) {
            ::close(sock);
            throw;
        }
    });

}

cocls::suspend_point<void> ContextIOImpl::mark_closing(SocketHandle s) {
    return _disp->mark_closing(s);
}


cocls::suspend_point<void> ContextIOImpl::stop() {
    if (_disp) {
        return _disp->mark_closing_all();
    } else {
        return {};
    }
}


static cocls::generator<Stream> listen_generator(SocketSupport ctx,
        TimeoutSettings tmcfg,
        std::stop_token stoken,
        ListeningSocketHandle h,
        int group_id) {

    std::stop_callback stopcb(stoken, [&]{
        ctx.mark_closing(h);
    });

    bool run = true;
    while (run) {
        WaitResult res = co_await ctx.io_wait(h,AsyncOperation::accept,std::chrono::system_clock::time_point::max());
        switch (res) {
            case WaitResult::timeout:
            case WaitResult::closed: run = false; break;
            default:
            case WaitResult::complete:
            case WaitResult::error: {
                sockaddr_storage addr;
                socklen_t slen = sizeof(addr);
                int s = ::accept4(h, reinterpret_cast<sockaddr *>(&addr), &slen,
                        SOCK_NONBLOCK|SOCK_CLOEXEC);
                if (s>=0) {
                    co_yield Stream ( std::make_shared<SocketStream>(ctx, s,
                            PeerName::from_sockaddr(&addr).set_group_id(group_id),
                            tmcfg));
                } else {

                    int e = errno;
                    if (e != EINTR) {
                        throw std::system_error(errno, std::system_category(), "::accept4");
                    }
                }
            };break;
        }
    }
    ctx.close(h);
}



cocls::generator<Stream> ContextIO::accept(std::vector<PeerName> &list,
                                std::stop_token token, TimeoutSettings tms) {

    std::vector<cocls::generator<Stream> > gens;
    std::vector<SocketHandle> handles;
    SocketSupport sup = *this;
    for (PeerName &x: list) {
        try {
            SocketHandle h = ContextIO::listen_socket(x);
            int id =x.get_group_id();
            x = PeerName::from_socket(h,false).set_group_id(id);
            gens.push_back(listen_generator(sup, tms, token, h, id));
            handles.push_back(h);
        } catch (...) {
            for (SocketHandle x: handles) {
                sup.close(x);
            }
            throw;
        }
    }

    return cocls::generator_aggregator(std::move(gens));
}

cocls::generator<Stream> ContextIO::accept(std::vector<PeerName> &&list,
                                std::stop_token token, TimeoutSettings tms) {
    return accept(list,std::move(token),std::move(tms));
}


struct ConnectInfo {
    const PeerName &peer;
    std::optional<SocketHandle> socket;
};

//coroutine which waits f
static cocls::async<void> wait_connect(ContextIO ctx,
            const PeerName &peer,
            int delay_sec,
            int timeout,
            std::stop_token stop,
            cocls::queue<ConnectInfo> &result) {

    SocketSupport supp = ctx;
    SocketHandle socket = -1;

    WaitResult res;
    //if delay_sec is nonzero - add some delay
    if (delay_sec) {
        //wait for delay - store future - to solve race condition
        auto dl = supp.wait_for(std::chrono::seconds(delay_sec), &socket);
        //register stop callback, which will cancel our delay
        std::stop_callback stpcb(stop, [&]{supp.cancel_wait(&socket);});
        //check stop requested finally, before wait, it would appear before registration
        if (stop.stop_requested()) {
            //if requested, cancel our wait
            supp.cancel_wait(&socket);
        }
        //in all case, wait for delay, retrieve result
        res = co_await dl;
        //if cancel is complete, report failure
        if (res == WaitResult::complete) {
            co_await result.push(ConnectInfo{peer, {}});
            //and exit
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

    //section where we can work with socket
    {
        //in this state, we can use socket handle to mark it closing when stop is requested
        std::stop_callback stpcb(stop, [&]{supp.mark_closing(socket);});
        //check stop request now, we can continue, if there is no stop request
        if (!stop.stop_requested()) {
            //wait for connect
            res = co_await supp.io_wait(socket,AsyncOperation::connect, TimeoutSettings::from_duration(timeout));
        }
    }
    //if connection is complete,
    if (res == WaitResult::complete) {
        //signal success
        co_await result.push(ConnectInfo{peer, socket});
        //and exit
        co_return;
    }

    //in all failures
    //close socket
    supp.close(socket);
    //report faulure
    co_await result.push(ConnectInfo{peer, {}});
}

cocls::future<Stream> ContextIO::connect(std::vector<PeerName> list, int timeout_ms, TimeoutSettings tms) {
    //queue collects results for multiple sockets
    cocls::queue<ConnectInfo> results;
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
        ConnectInfo nfo = co_await results.pop();
        //if socket is set, then connection was successful
        if (nfo.socket.has_value()) {
            //but if we don't have stream
            if (!connected.has_value()) {
                //create it now
                connected = Stream(std::make_shared<SocketStream>(*this, *nfo.socket, nfo.peer, tms));
                //and stop other attempts
                stop.request_stop();
            } else {
                //this can happen as race condition when two connections are ready at the same time
                //so close any other connection
                _ptr->close(*nfo.socket);
            }
        }
    }
    //finally if we retrieved no connection, report exception
    if (!connected.has_value()) throw ConnectFailedException();
    //otherwise, return stream
    co_return std::move(*connected);
}


ContextIO ContextIO::create(std::shared_ptr<cocls::thread_pool> pool) {
    return ContextIO(std::make_shared<ContextIOImpl>(pool));
}

ContextIO ContextIO::create(std::size_t iothreads){
    return ContextIO(std::make_shared<ContextIOImpl>(iothreads));
}

cocls::suspend_point<void> ContextIO::stop() {
   if (_ptr) {
       cocls::suspend_point<void> r = _ptr->stop();
       _ptr.reset();
       return r;
   } else {
       return {};
   }
}


cocls::future<WaitResult> ContextIOImpl::io_wait(SocketHandle handle,
        AsyncOperation op, std::chrono::system_clock::time_point timeout) {

    return [&](auto p){_disp->async_wait(op, handle, std::move(p), timeout);};
}

cocls::future<WaitResult> ContextIOImpl::wait_until(
        std::chrono::system_clock::time_point tp, const void *ident) {
    return [&](auto promise) {
        _disp->schedule(ident, std::move(promise), tp);
    };
}

cocls::suspend_point<bool> ContextIOImpl::cancel_wait(const void *ident) {
    return _disp->cancel_schedule(ident);
}

}

