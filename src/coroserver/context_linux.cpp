#include "context_linux.h"

#include <arpa/inet.h>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/signalfd.h>
#include <sys/un.h>
#include <netdb.h>
#include <fcntl.h>
#include <map>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>


namespace coroserver {


coro::prepared_coro TimerHandleData::sleep_until(std::chrono::system_clock::time_point tp, coro::awaitable<bool>::result p) {
   if (_shutted_down) {
       return p(false);
   }
   _tp = tp;
   _p = std::move(p);
   return {};
}

coro::prepared_coro TimerHandleData::on_timeout(std::chrono::system_clock::time_point tp) {
   if (tp <= _tp) {
       _tp = std::chrono::system_clock::time_point::max();
       return _p(true);
   }
   return {};
}

coro::prepared_coro TimerHandleData::on_shutdown() {
   _shutted_down = true;
   _tp = std::chrono::system_clock::time_point::max();
   return _p(false);
}

int ServerHandleData::do_accept_sync() {
    int r = ::accept4(_socket,NULL, NULL, SOCK_CLOEXEC| SOCK_NONBLOCK);
    if (r == -1) {
        int e = errno;
        if (e == EWOULDBLOCK) {
            return -1;
        } else {
            throw std::system_error(e, std::system_category(), "accept4 failed");
        }
    }
    return r;
}

coro::prepared_coro ServerHandleData::do_accept_async(
        std::chrono::system_clock::time_point tp,
        coro::awaitable<Context::Handle>::result p) {
    if (_shutted_down) {
        return p(0);
    }
    _tp = tp;
    _p = std::move(p);
    _flags = EPOLLIN|EPOLLONESHOT;
    return {};
}

template<std::invocable<int> HandleCreation>
coro::prepared_coro ServerHandleData::on_complete(HandleCreation &&hc) {
    try {
        _flags = -1;
        int i = do_accept_sync();
        if (i == -1) {
            return do_accept_async(_tp, std::move(_p));
        }
        _tp = std::chrono::system_clock::time_point::max();
        return _p(hc(i));
    } catch (...) {
        _tp = std::chrono::system_clock::time_point::max();
        return _p.set_exception(std::current_exception());
    }
}

coro::prepared_coro ServerHandleData::on_timeout(std::chrono::system_clock::time_point tp) {
    if (tp <= _tp) {
        _flags = -1;
        _tp = std::chrono::system_clock::time_point::max();
        return _p(0);
    }
    return {};
}

coro::prepared_coro ServerHandleData::on_shutdown() {
   _shutted_down = true;
   _tp = std::chrono::system_clock::time_point::max();
   return _p(false);
}

void ContextImpl::update_epoll_flags(Handle h, const SocketHandleData &pb) {
    int socket = pb.get_socket();
    int flags = pb.get_flags();
    if (flags != -1) {
        _epoll.mod(socket, flags, h);
    }

}

void ContextImpl::thread_entry_point() {
    std::vector<coro::prepared_coro> prepared;
    std::unique_lock lk(_mx);
    auto my_thread_id = ++_thread_counter;
    while (!_stop_signaled) {
        if (_new_tp < _awaiting_tp) {
            _awaiting_tp = _new_tp;
            _awaiting_thread = my_thread_id;
        }
        auto now = std::chrono::system_clock::now();
        if (_awaiting_thread == my_thread_id && now >= _awaiting_tp) {
            auto tp = std::chrono::system_clock::time_point::max();
            for (auto &hm : _handleMap) {
                auto ctp = hm._value->get_timeout();
                if (ctp <= now) {
                    TwoCoros r = hm._value->visit([&](auto &p)->TwoCoros{
                        auto r = p.on_timeout(now);
                        if constexpr(std::is_base_of_v<SocketHandleData, std::decay<decltype(p)> >) {
                            update_epoll_flags(hm._handle, p);
                        }
                        return r;
                    });
                    if (r.a) prepared.push_back(std::move(r.a));
                    if (r.b) prepared.push_back(std::move(r.b));
                } else {
                    if (ctp < tp) tp = ctp;
                }
            }
            _awaiting_tp = tp;
        }
        if (prepared.empty()) {
            lk.unlock();
            std::optional<EPoll<Handle>::WaitRes> wr = _epoll.wait(_awaiting_tp);
            if (wr) {
                lk.lock();
                Handle h = wr->ident;
                auto iter = _handleMap.find(h);
                if (iter != _handleMap.end()) {
                    TwoCoros r = iter->_value->visit([&](auto &p) -> TwoCoros {
                        using T = std::decay_t<decltype(p)>;
                        TwoCoros cr;
                        if constexpr(std::is_same_v<T, ServerHandleData>) {
                            cr.a =  p.on_complete([&](int socket){
                                return create_stream(socket);
                            });
                        } else if constexpr(std::is_same_v<T, StreamHandleData>) {
                            if (wr->events & EPOLLIN) {
                                cr.a = p.on_complete_recv();
                            }
                            if (wr->events & EPOLLOUT) {
                                cr.b = p.on_complete_send();
                            }
                        }
                        if constexpr(std::is_base_of_v<SocketHandleData, T>) {
                            update_epoll_flags(h, p);
                        }
                        return cr;
                    });
                    if (r.a) prepared.push_back(std::move(r.a));
                    if (r.b) prepared.push_back(std::move(r.b));
                }
                lk.unlock();
            }
        }
        prepared.clear();   //execute prepared
    }
    _epoll_wk.set();    //wake up other threads
}

void ContextImpl::shutdown(Handle h) {
    TwoCoros r;
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(h);
    if (iter != _handleMap.end()) {
        r = iter->_value->visit([&](auto &p) -> TwoCoros {
           return p.on_shutdown();
        });
    }
}

void ContextImpl::close(Handle h) {
    TwoCoros r;
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(h);
    if (iter != _handleMap.end()) {
        r = iter->_value->visit([&](auto &p) -> TwoCoros {
            using T = std::decay_t<decltype(p)>;
           TwoCoros r =  p.on_shutdown();
           if constexpr(std::is_base_of_v<SocketHandleData, T>) {
               SocketHandleData &pb = p;
               int socket = pb.get_socket();
               _epoll.del(socket);
               ::close(socket);
           }
           return r;
        });
        _handleMap.erase(iter);
    }
}

ContextImpl::Handle ContextImpl::create_timer() {
    std::lock_guard _(_mx);
    Handle h = _handleMap.insert(std::make_unique<TimerHandleData>());
    return h;
}

coro::awaitable<bool> ContextImpl::sleep(Handle timer,
        std::chrono::system_clock::time_point tp) {
    if (tp <= std::chrono::system_clock::now()) return true;
    return [this, timer, tp](coro::awaitable<bool>::result p) -> coro::prepared_coro{
        std::lock_guard _(_mx);
        auto iter = _handleMap.find(timer);
        if (iter == _handleMap.end() || typeid(*iter->_value) != typeid(TimerHandleData)) return p(false);
        auto tm = static_cast<TimerHandleData *>(iter->_value.get());
        auto r =  tm->sleep_until(tp, std::move(p));
        update_timeout(*tm);
        return r;
    };
}

coro::awaitable<ContextImpl::Handle> ContextImpl::accept(Handle server, std::chrono::system_clock::time_point timeout) {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(server);
    if (iter == _handleMap.end() || typeid(*iter->_value) != typeid(ServerHandleData)) return null_handle;
    auto *srv = static_cast<ServerHandleData *>(iter->_value.get());
    int socket = srv->do_accept_sync();
    if (socket>=0) {
        return create_stream(socket);
    } else {
        return [this, server, timeout](coro::awaitable<Handle>::result p) -> coro::prepared_coro {
            std::lock_guard _(_mx);
            auto iter = _handleMap.find(server);
            auto *srv = static_cast<ServerHandleData *>(iter->_value.get());
            auto r =  srv->do_accept_async(timeout, std::move(p));
            update_epoll_flags(server, *srv);
            update_timeout( *srv);
            return r;
        };
    }

}

coro::awaitable<std::size_t> ContextImpl::receive(Handle stream, char *buffer,
        std::size_t sz, std::chrono::system_clock::time_point timeout) {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(stream);
    if (iter == _handleMap.end() || typeid(*iter->_value) != typeid(StreamHandleData)) return 0;
    auto *strm = static_cast<StreamHandleData *>(iter->_value.get());
    int r = strm->recv_sync(buffer, sz);
    if (r >= 0) {
        return static_cast<std::size_t>(r);
    } else {
        return [this, stream, timeout](coro::awaitable<std::size_t>::result p) {
            std::lock_guard _(_mx);
            auto iter = _handleMap.find(stream);
            auto *strm = static_cast<StreamHandleData *>(iter->_value.get());
            auto r = strm->recv_async(timeout, std::move(p));
            update_epoll_flags(stream, *strm);
            update_timeout(*strm);
            return r;
        };
    }
}

coro::awaitable<bool> ContextImpl::send(Handle stream, const char *buffer,
        std::size_t sz, std::chrono::system_clock::time_point timeout) {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(stream);
    if (iter == _handleMap.end() || typeid(*iter->_value) != typeid(StreamHandleData)) return false;
    auto *strm = static_cast<StreamHandleData *>(iter->_value.get());
    int r = strm->send_sync(buffer, sz);
    if (r > 0) {
        return true;
    } else if (r == 0) {
        return false;
    } else {
        return [this, stream, timeout](coro::awaitable<bool>::result p) {
            std::lock_guard _(_mx);
            auto iter = _handleMap.find(stream);
            auto *strm = static_cast<StreamHandleData *>(iter->_value.get());
            auto r =  strm->send_async(timeout, std::move(p));
            update_epoll_flags(stream, *strm);
            update_timeout(*strm);
            return r;

        };
    }
}

void ContextImpl::update_timeout(const AbstractHandleData &hd) {
    auto tm = hd.get_timeout();
    if (tm < _new_tp) {
        _new_tp = tm;
        _epoll_wk.set();
    }
}

coro::awaitable<bool> ContextImpl::send_eof(Handle stream) {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(stream);
    if (iter == _handleMap.end() || typeid(*iter->_value) != typeid(StreamHandleData)) return false;
    auto *strm = static_cast<StreamHandleData *>(iter->_value.get());
    int socket = strm->get_socket();
    int r = ::shutdown(socket, SHUT_WR);
    if (r == -1) {
        throw std::system_error(errno, std::system_category(), "socket shutdown (send_eof)");
    }
    return true;
}

void ContextImpl::signal_stop() {
    std::lock_guard _(_mx);
    _stop_signaled = true;
    _epoll_wk.set();
}

StreamState ContextImpl::get_state(Handle h) {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(h);
    if (iter == _handleMap.end()) return StreamState::closed;
    return iter->_value->visit([&](const auto &p){
        return p.get_state();
    });

}

static std::string sockaddrToString(const sockaddr* addr) {
    if (addr == nullptr) {
        return "unknown";
    }

    switch (addr->sa_family) {
        case AF_INET: {
            const sockaddr_in* ipv4 = reinterpret_cast<const sockaddr_in*>(addr);
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(ipv4->sin_addr), ip_str, INET_ADDRSTRLEN);
            return std::string(ip_str) + ":" + std::to_string(ntohs(ipv4->sin_port));
        }

        case AF_INET6: {
            const sockaddr_in6* ipv6 = reinterpret_cast<const sockaddr_in6*>(addr);
            char ip_str[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &(ipv6->sin6_addr), ip_str, INET6_ADDRSTRLEN);
            return "[" + std::string(ip_str) + "]:" + std::to_string(ntohs(ipv6->sin6_port));
        }

        case AF_UNIX: {
            const sockaddr_un* un = reinterpret_cast<const sockaddr_un*>(addr);
            return "unix://" + std::string(un->sun_path);
        }

        default:
            return "unknown";
    }
}

std::string ContextImpl::get_host(Handle h) const {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(h);
    if (iter == _handleMap.end()) return {};
    return iter->_value->visit([&](const auto &p) -> std::string {
        using T = std::decay_t<decltype(p)>;
        sockaddr_storage sock_stor;
        socklen_t slen = sizeof(sock_stor);
       if constexpr(std::is_same_v<ServerHandleData, T>) {
           int socket = p.get_socket();
           if (getsockname(socket, reinterpret_cast<sockaddr *>(&sock_stor), &slen) == 0) {
               return sockaddrToString(reinterpret_cast<sockaddr *>(&sock_stor));
           } else {
               return {};
           }
       } else {
           return {};
       }
    });
}


int is_socket_active(const char *socket_path) {
    int sockfd;
    struct sockaddr_un addr;

    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return -1; // Chyba při vytváření socketu
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        close(sockfd);
        return 1;
    } else {
        int err = errno;
        close(sockfd);
        if (err == ECONNREFUSED) {
            return 0;
        } else if (err == ENOENT) {
            return 0;
        } else {
            return -1;
        }
    }
}

struct unix_addr_info :  addrinfo {
    sockaddr_un sun;
};

std::shared_ptr<struct addrinfo> resolve_unix_socket(std::string host, bool passive) {
    char buff[130] = {};
    if (host.size() >= sizeof(buff)) throw std::invalid_argument("Socket path is too long");
    strncpy(buff, host.c_str(), sizeof(buff)-1);
    char *path = buff+6;
    char *sep = strrchr(path, ':');
    int rights = 0;
    if (sep != nullptr && passive) {
        char *end;
        rights = static_cast<int>(strtoul(sep+1,&end,8));
        if (*end) throw std::invalid_argument("Invalid chmod part in unix socket spec");
        *sep = 0;
    }

    if (passive && !is_socket_active(path)) {
        unlink(path);
    }

    sockaddr_un sun = {};
    sun.sun_family = AF_UNIX;
    strncpy(sun.sun_path, path, sizeof(sun.sun_path)-1);
    std::shared_ptr<unix_addr_info> a = std::make_shared<unix_addr_info>();
    a->sun = sun;
    a->ai_addr = reinterpret_cast<sockaddr *>(&a->sun);
    a->ai_addrlen = sizeof (a->sun);
    a->ai_family = AF_UNIX;
    a->ai_flags = rights;
    a->ai_protocol = 0;
    a->ai_socktype = 0;
    a->ai_canonname = a->sun.sun_path;
    return a;
}


std::shared_ptr<struct addrinfo> dns_resolve(std::string host, std::string def_port, bool passive) {
    if (!host.empty()) {
        std::string_view vhost = host;
        if (vhost.substr(0,7) == "unix://") {
            return resolve_unix_socket(std::move(host), passive);
        }
        if (vhost.front() == '[')   { //ipv6 addr
            auto sep = vhost.find(']');
            if (sep == vhost.npos) {
                throw std::invalid_argument("Incomplete IPv6 address");
            }
            auto sep2 = vhost.find(':', sep);
            if (sep2 != vhost.npos) {
                def_port = std::string(vhost.substr(sep2+1));
            }
            host = std::string(vhost.substr(1, sep-1));
        } else {
            auto sep = vhost.rfind(':');
            if (sep != vhost.npos) {
                def_port = std::string(vhost.substr(sep+1));
                host.resize(sep);
            }
        }
    }

    struct addrinfo req =  {};
    req.ai_family = AF_UNSPEC;
    req.ai_socktype = SOCK_STREAM;
    req.ai_protocol = IPPROTO_TCP;
    req.ai_flags = (passive?AI_PASSIVE:0)|AI_ADDRCONFIG;
    struct addrinfo *out = nullptr;
    int r = getaddrinfo(host.empty()?nullptr:host.c_str(), def_port.c_str(), &req, &out);
    if (r != 0) {
        throw std::runtime_error("GAI Error: " + std::string(gai_strerror(r)));
    } else {
        return {out, [](auto a){freeaddrinfo(a);}};
    }
}


ContextImpl::Handle ContextImpl::create_server(std::string host, std::string def_port) {
    auto a =  dns_resolve(host, def_port, true);
    if (a == nullptr) throw std::system_error(ENOENT, std::system_category(), "DNS resolv failed");
    int s = socket(a->ai_family,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, a->ai_protocol);
    if (s < 0) throw std::system_error(errno, std::system_category(), "socket");
    int v = 1;
    if (a->ai_family == AF_INET || a->ai_family == AF_INET6) {
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(int));
    }
    try {
        if (bind(s, a->ai_addr, a->ai_addrlen) != 0) {
            throw std::system_error(errno, std::system_category(), "bind");
        }
        if (listen(s, SOMAXCONN) != 0) {
            throw std::system_error(errno, std::system_category(), "listen");
        }
        if (a->ai_family == AF_UNIX) {
            chmod(a->ai_canonname, a->ai_flags);
        }
        Handle h = _handleMap.insert(std::make_unique<ServerHandleData>(s));
        _epoll.add(s, 0, h);
        return h;
    } catch (...) {
        close(s);
        throw;
    }
}

ContextImpl::Handle ContextImpl::connect(std::string host, std::string def_port) {
    auto a = dns_resolve(host, def_port, false);
    if (a == nullptr) throw std::system_error(ENOENT, std::system_category(), "DNS resolv failed");
    int s = socket(a->ai_family,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, a->ai_protocol);
    if (s < 0) throw std::system_error(errno, std::system_category(), "socket");
    try {
        int r = ::connect(s, a->ai_addr, a->ai_addrlen);
        if (r < 0) {
            int e = errno;
            if (e != EWOULDBLOCK) {
                throw std::system_error(e, std::system_category(), "connect");
            }
        }
        Handle h = _handleMap.insert(std::make_unique<ServerHandleData>(s));
        _epoll.add(s, EPOLLOUT|EPOLLONESHOT, h);
        return h;
    } catch (...) {
        close(s);
        throw;
    }
}

ContextImpl::Handle ContextImpl::create_stream(int socket) {
    Handle h = _handleMap.insert(std::make_unique<StreamHandleData>(socket));
    _epoll.add(socket, EPOLLOUT|EPOLLONESHOT, h);
    return h;
}

int StreamHandleData::recv_sync(char *buffer, std::size_t sz) {
    this->_recv_buffer = buffer;
    this->_recv_buffer_size = sz;
    return do_recv();
}
int StreamHandleData::do_recv() {
    if (_state_eof) return 0;
    if (_connect_error) throw std::system_error(_connect_error, std::system_category(), "Connect error");
    int r = ::recv(_socket, this->_recv_buffer, this->_recv_buffer_size, MSG_DONTWAIT);
    if (r < 0) {
        int e = errno;
        if (e == EWOULDBLOCK) return -1;
        if (e == EPIPE) return 0;
        throw std::system_error(e, std::system_category(), "recv");
    }
    return r;
}

int StreamHandleData::send_sync(const char *buffer, std::size_t sz) {
    this->_send_buffer = buffer;
    this->_send_buffer_size = sz;
    return do_send();
}
int StreamHandleData::do_send() {
    if (_send_closed) return 0;
    if (_opening) return -1;
    if (_connect_error) throw std::system_error(_connect_error, std::system_category(), "Connect error");
    while (this->_send_buffer_size) {
        int r = ::send(_socket, this->_send_buffer, this->_send_buffer_size, MSG_DONTWAIT);
        if (r < 0) {
            int e = errno;
            if (e == EWOULDBLOCK) return -1;
            if (e == EPIPE) return 0;
            throw std::system_error(e, std::system_category(), "send");
        }
        if (r == 0) {
            _state_eof = 0;
            return 0;
        }
        this->_send_buffer += r;
        this->_send_buffer_size -= r;
    }
    return 1;
}

coro::prepared_coro StreamHandleData::recv_async(
                        std::chrono::system_clock::time_point tp,
                        coro::awaitable<std::size_t>::result p) {
    if (_shutted_down) return p(0);
    this->_recv_timeout = tp;
    this->_recv_result = std::move(p);
    update_timeout();
    update_flags();
    return {};

}

coro::prepared_coro StreamHandleData::send_async(
                        std::chrono::system_clock::time_point tp,
                        coro::awaitable<bool>::result p) {

    if (_shutted_down) return p(false);
    this->_send_timeout = tp;
    this->_send_result = std::move(p);
    update_timeout();
    update_flags();
    return {};

}

coro::prepared_coro StreamHandleData::on_complete_recv() {
    coro::prepared_coro out;
    try {

        if (_recv_result) {
            int r = do_recv();
            if (r >= 0) {
                out = _recv_result.set_value(r);
                _recv_timeout = std::chrono::system_clock::time_point::max();
                update_timeout();
                update_flags();
            }
        }

    } catch (...) {
        out = _recv_result.set_exception(std::current_exception());
    }
    return out;
}

coro::prepared_coro StreamHandleData::on_complete_send() {
    coro::prepared_coro out;
    try {

        if (_opening) {
            _connect_error = 0;;
            socklen_t len = sizeof(_connect_error);
            _opening = false;
            if (getsockopt(_socket, SOL_SOCKET, SO_ERROR, &_connect_error, &len) < 0) {
                _connect_error = errno;
            }
        }

        if (_send_result) {
            int r = do_send();
            if (r >= 0) {
                out = _send_result.set_value(r);
                _send_timeout = std::chrono::system_clock::time_point::max();
                update_timeout();
                update_flags();
            }
        }

    } catch (...) {
        out = _recv_result.set_exception(std::current_exception());
    }
    return out;
}

TwoCoros StreamHandleData::on_timeout(std::chrono::system_clock::time_point tp) {
    TwoCoros out;
    if (tp >= _recv_timeout) {
        out.a = _recv_result.set_empty();
        _recv_timeout =std::chrono::system_clock::time_point::max();
    }
    if (tp >= _send_timeout) {
        out.b = _send_result.set_empty();
        _send_timeout =std::chrono::system_clock::time_point::max();
    }
    update_flags();
    update_timeout();
    return out;
}

TwoCoros StreamHandleData::on_shutdown() {
    TwoCoros out;
    _shutted_down = true;
    out.a = _recv_result.set_value(0);
    out.b = _send_result.set_value(false);
    _recv_timeout = std::chrono::system_clock::time_point::max();
    _send_timeout = std::chrono::system_clock::time_point::max();
    update_flags();
    update_timeout();
    return out;
}

StreamState StreamHandleData::get_state() const {
    if (_connect_error || _state_eof) return StreamState::closed;
    if (_send_closed) return StreamState::closing;
    if (_opening) return StreamState::opening;
    return StreamState::active;
}

void StreamHandleData::update_timeout() {
    _tp = std::min(_send_timeout, _recv_timeout);
}

void StreamHandleData::update_flags() {
    _flags = 0;
    if (_send_result) _flags |= EPOLLOUT|EPOLLONESHOT;
    if (_recv_result) _flags |= EPOLLIN|EPOLLONESHOT;
}

}

