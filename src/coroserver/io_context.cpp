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

ListeningSocketHandle ContextIOImpl::listen(const PeerName &addr) {
    return addr.use_sockaddr([&](const sockaddr *saddr, socklen_t slen) {
        int sock = ::socket(saddr->sa_family, SOCK_STREAM, saddr->sa_family == AF_UNIX?0:IPPROTO_TCP);
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


#if 0
cocls::future_with_context<Stream, ContextIO::ConnectCtx> ContextIO::connect(PeerName addr,
                                        unsigned int connect_timeout_ms,
                                        TimeoutSettings tms) {
    return {*this,std::move(addr), connect_timeout_ms, std::move(tms) };
}

ContextIO::ConnectCtx::ConnectCtx(cocls::promise<Stream> &&promise,
                                  std::shared_ptr<ContextIOImpl> ctx,
                                  PeerName && addr,
                                  unsigned int connect_timeout_ms,
                                  TimeoutSettings tms)
       :promise(std::move(promise))
       ,ctx(ctx)
       ,addr(std::move(addr))
       ,tms(std::move(tms))
       ,h(addr.connect())
{
    auto tp = TimeoutSettings::fromDuration(connect_timeout_ms);
    await([&]{
       return ctx->io_wait(h, AsyncOperation::connect, tp);
    });
}

void ContextIO::ConnectCtx::resume() noexcept {
    try {
        WaitResult res = value();
        switch (res) {
            case WaitResult::complete:
                promise(Stream(std::make_shared<SocketStream>(ctx, h, std::move(addr), tms)));
                return;
            case WaitResult::timeout:
                throw TimeoutException();
            default:
            case WaitResult::closed:
            case WaitResult::error:
                break;
        }
        throw ConnectFailedException();
    } catch (...) {
        promise(std::current_exception());
    }
}

#endif

void ContextIOImpl::mark_closing(SocketHandle s) {
    _disp->mark_closing(s);
}


void ContextIOImpl::stop() {
    if (_disp) {
        _disp->mark_closing_all();
    }
}


static cocls::generator<Stream> listen_generator(ContextIO ctx,
        TimeoutSettings tmcfg,
        std::stop_token stoken,
        ListeningSocketHandle h) {

    std::stop_callback stopcb(stoken, [&]{
        ctx->mark_closing(h);
    });

    bool run = true;
    while (run) {
        WaitResult res = co_await ctx->io_wait(h,AsyncOperation::accept,std::chrono::system_clock::time_point::max());
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
                            PeerName::from_sockaddr(&addr),
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
    ctx->close(h);
}


#if 0

cocls::generator<Stream> ContextIO::accept(NetAddrList list, std::stop_token token, TimeoutSettings tms) {
    std::vector<cocls::generator<Stream> > gens;
    std::vector<SocketHandle> handles;
    for (const PeerName &x: list) {
        try {
            SocketHandle h = x.listen();
            gens.push_back(listen_generator(*this, tms, token, h));
            handles.push_back(h);
        } catch (...) {
            for (SocketHandle x: handles) {
                (*this)->close(x);
            }
            throw;
        }
    }

    return cocls::generator_aggregator(std::move(gens));
}

#endif

ContextIO ContextIO::create(std::shared_ptr<cocls::thread_pool> pool) {
    return ContextIO(std::make_shared<ContextIOImpl>(pool));
}

ContextIO ContextIO::create(std::size_t iothreads){
    return ContextIO(std::make_shared<ContextIOImpl>(iothreads));
}

void ContextIO::stop() {
   if (*this) (*this)->stop();
}

#if 0

static cocls::task<void> server_cycle(cocls::generator<Stream> master, ContextIO::CoMain co_main_fn) {

    bool b = co_await master.next();
    while (b) {
        co_main_fn(std::move(master.value()));
        b = co_await master.next();
    }
    co_return;
}

cocls::task<void> ContextIO::tcp_server(CoMain &&co_main_fn,
                  NetAddrList list,
                  std::stop_token stop_token,
                  TimeoutSettings tms) {
    return server_cycle(this->accept(list, std::move(stop_token), tms), std::move(co_main_fn));

}


future<Stream> ContextIO::connect(NetAddrList list, unsigned int connect_timeout_ms, TimeoutSettings tms) {
    std::exception_ptr e;
    for (auto &a: list) {
        try {
            co_return co_await connect(std::move(a), connect_timeout_ms, tms);
        } catch (...) {
            e = std::current_exception();
        }
    }
    if (e) std::rethrow_exception(e);
    else throw ConnectFailedException();
}

future<WaitResult> ContextIOImpl::io_wait(SocketHandle handle,
        AsyncOperation op, std::chrono::system_clock::time_point timeout) {

    return [&](auto p){_disp->async_wait(op, handle, std::move(p), timeout);};
}

DatagramExchange ContextIO::create_datagram_exchange(PeerName addr, TimeoutSettings tms) {
    SocketHandle h = addr.bind_udp();
    return std::make_shared<SocketDatagramExchange>(*this, h, tms);
}

future<WaitResult> ContextIOImpl::wait_until(
        std::chrono::system_clock::time_point tp, const void *ident) {
    return [&](auto promise) {
        _disp->schedule(ident, std::move(promise), tp);
    };
}

bool ContextIOImpl::cancel_wait(const void *ident) {
    return _disp->cancel_schedule(ident);
}

DatagramRouter ContextIO::create_datagram_router(PeerName bind_addr,
                                    bool dedup_messages, TimeoutSettings tms) {
    return DatagramRouter(std::make_shared<DatagramRouterImpl>(
            *this, create_datagram_exchange(bind_addr, {}),
            tms, dedup_messages));

}

class UDPStream: public AbstractStream, public SocketDatagramExchange {
public:
    UDPStream(ContextIO ctx, SocketHandle h, TimeoutSettings tms, PeerName peer, bool dedup)
        :SocketDatagramExchange(ctx,h,tms)
        ,_peer(std::move(peer))
        ,_dedup(dedup) {}

    virtual userver::PeerName get_source() const override {
        return _peer;
    }
    virtual cocls::future<std::string_view> read() override {
        auto tmp = read_putback();
        if (!tmp.empty()) return cocls::future<std::string_view>::set_value(tmp);
        return read_coro(_read_storage);
    }
    virtual cocls::future<bool> write(const std::string_view &data) override {
        if (data.empty()) return cocls::future<bool>::set_value(true);
        return write_coro(_write_storage, data);
    }
    virtual cocls::future<void> write_eof() override {
        return write_coro(_write_storage);
    }
    virtual void shutdown() override {
        SocketDatagramExchange::shutdown();
    }

    virtual TimeoutSettings get_timeouts() override {
        return SocketDatagramExchange::get_timeouts();
    }
    virtual void set_timeouts(const userver::TimeoutSettings &tm) override {
        return SocketDatagramExchange::set_timeouts(tm);
    }

protected:
    PeerName _peer;
    bool _dedup;

    cocls::reusable_storage _read_storage;
    cocls::reusable_storage _write_storage;
    cocls::mutex _writemx;

    std::vector<char> _prev_msg;
    std::vector<char> _prev_resp;

    cocls::with_allocator<cocls::reusable_storage, cocls::async<std::string_view> >
     read_coro(cocls::reusable_storage &) {

        while(true) {
            auto f = receive();
            if (co_await f.has_value()) {
                Datagram dgram = *f;
                if (!dgram.peer.equal_to(_peer)) continue;
                if (dgram.data.empty()) co_return std::string_view();
                if (_dedup && dgram.data == std::string_view(_prev_msg.data(), _prev_msg.size())) {
                    auto own = co_await _writemx.lock();
                    if (!_prev_msg.empty()) {
                       co_await send(Datagram{_peer, std::string_view(_prev_resp.data(), _prev_resp.size())});
                       continue;
                    }
                }
                if (_dedup) {
                    _prev_msg.clear();
                    _prev_msg.resize(dgram.data.size());
                    std::copy(dgram.data.begin(), dgram.data.end(), _prev_msg.begin());
                }
                co_return dgram.data;
            } else {
                co_return std::string_view();
            }
        }
    }

    cocls::with_allocator<cocls::reusable_storage, cocls::async<bool> >
     write_coro(cocls::reusable_storage &, std::string_view msg) {
        auto own = co_await _writemx.lock();
        if (_dedup){
            _prev_resp.clear();
            _prev_resp.resize(msg.size());
            std::copy(msg.begin(), msg.end(), _prev_resp.begin());
        }
        co_return co_await send(Datagram{_peer, msg});
    }

    cocls::with_allocator<cocls::reusable_storage, cocls::async<void> >
     write_coro(cocls::reusable_storage &) {
        auto own = co_await _writemx.lock();
        co_await send(Datagram{_peer, {}});
    }


};

Stream ContextIO::connect_udp(PeerName remote_addr, bool dedup_messages,
                                    TimeoutSettings tms) {
    auto s = std::make_shared<UDPStream>(*this, remote_addr.auto_bind_udp(), tms, remote_addr, dedup_messages);
    return Stream(s);

}

#endif

}

