#include "context_linux.hpp"
#include "context_inc.hpp"
#include "process.hpp"
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
#include <numeric>
#include "context_windows.hpp"


extern char **environ;

namespace coroserver {


class ChildManagerSingleton {
public:

    ChildManagerSingleton();
    ~ChildManagerSingleton();

    static ChildManagerSingleton &getInstance();

    coro::prepared_coro on_event();
    coro::prepared_coro reg(pid_t p, TwoPipesStreamData *owner);
    void unreg(pid_t p);
    int get_fd() const {return _eventfd;}

    static void handleSigChld(int signo);

protected:
    std::vector<std::pair<pid_t, TwoPipesStreamData *>  > _pidmap;
    std::mutex _mx;
    int _eventfd;
};

class BreakManagerSingleton {
public:
    BreakManagerSingleton();
    ~BreakManagerSingleton();

    static BreakManagerSingleton &getInstance();

    TwoCoros on_event();
    void reg(coro::awaitable<BreakType>::result r);
    int get_fd() const {return _eventfd;}

    static void handleSig(int signo);

protected:
    std::vector<coro::awaitable<BreakType>::result> _awts;
    std::mutex _mx;
    int _eventfd;
    int _signal = 0;

};

constexpr auto max_timeout = std::chrono::system_clock::time_point::max();


coro::prepared_coro TimerHandleData::sleep_until(std::chrono::system_clock::time_point tp, coro::awaitable<bool>::result p) {
   if (_was_shutdown) {
       return p(false);
   }
   _tp = tp;
   _p = std::move(p);
   return {};
}

coro::prepared_coro TimerHandleData::on_timeout(std::chrono::system_clock::time_point tp) {
   if (tp <= _tp) {
       _tp = max_timeout;
       return _p(true);
   }
   return {};
}

coro::prepared_coro TimerHandleData::on_shutdown() {
   _was_shutdown = true;
   _tp = max_timeout;
   return _p(false);
}

SocketHandleData::~SocketHandleData() {
    if (_socket >= 0) ::close(_socket);
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

template<std::invocable<int, int> Svc>
void ServerHandleData::apply_epoll_flags(Svc &&svc) const {
    if (_p) {
        svc(_socket, EPOLLIN|EPOLLONESHOT);
    } else {
        svc(_socket, 0);
    }

}


coro::prepared_coro ServerHandleData::do_accept_async(
        std::chrono::system_clock::time_point tp,
        coro::awaitable<Context::Handle>::result p) {
    if (_was_shutdown) {
        return p(0);
    }
    _tp = tp;
    _p = std::move(p);
    return {};
}

ServerHandleData::ServerHandleData(int socket, ContextImpl *ctx)
    :SocketHandleData(HandleType::server, socket), _ctx(ctx) {}

coro::prepared_coro ServerHandleData::on_complete(int /*flags*/) {
    try {
        int i = do_accept_sync();
        if (i == -1) {
            return do_accept_async(_tp, std::move(_p));
        }
        _tp = max_timeout;
        return _p(_ctx->create_stream(i));
    } catch (...) {
        _tp = max_timeout;
        return _p.set_exception(std::current_exception());
    }
}

coro::prepared_coro ServerHandleData::on_timeout(std::chrono::system_clock::time_point tp) {
    if (tp <= _tp) {
        _tp = max_timeout;
        return _p(0);
    }
    return {};
}

coro::prepared_coro ServerHandleData::on_shutdown() {
   _was_shutdown = true;
   _tp = max_timeout;
   return _p(false);
}

ContextImpl::ContextImpl() {
    _epoll.add(_epoll_wk.get_fd(),EPOLLIN|EPOLLONESHOT,null_handle);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

}

void ContextImpl::update_epoll_flags(Handle h, const AbstractHandleData &pb) {
    pb.visit([&](const auto &b){
        b.apply_epoll_flags([&](int socket, int flags) {
            _epoll.mod(socket, flags, h);
        });
    });

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
            auto tp = max_timeout;
            for (auto &hm : _handleMap) {
                hm._value->visit([&](auto &p) {
                    auto ctp = p.get_timeout();
                    if (ctp <= now) {
                        TwoCoros r = p.on_timeout(now);
                        update_epoll_flags(hm._handle, p);
                        if (r.a) prepared.push_back(std::move(r.a));
                        if (r.b) prepared.push_back(std::move(r.b));
                    } else {
                        if (ctp < tp) tp = ctp;
                    }
                });
            }
            _awaiting_tp = tp;
        }
        if (prepared.empty()) {
            lk.unlock();
            std::optional<EPoll<Handle>::WaitRes> wr = _epoll.wait(_awaiting_tp);
            if (wr) {
                lk.lock();
                Handle h = wr->ident;
                if (h == null_handle) {
                    _epoll_wk.read_and_clear();
                    _epoll.mod(_epoll_wk.get_fd(),EPOLLIN|EPOLLONESHOT,null_handle);
                } else {
                    auto iter = _handleMap.find(h);
                    if (iter != _handleMap.end()) {
                        TwoCoros r = iter->_value->visit([&](auto &p) -> TwoCoros {
                            TwoCoros cr = p.on_complete(wr->events);
                            update_epoll_flags(h, p);
                            return cr;
                        });
                        if (r.a) prepared.push_back(std::move(r.a));
                        if (r.b) prepared.push_back(std::move(r.b));
                    }
                }
                lk.unlock();
            }
        }
        prepared.clear();   //execute prepared
        lk.lock();
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

ContextImpl::Handle ContextImpl::create_timer() {
    std::lock_guard _(_mx);
    Handle h = _handleMap.insert(PHandleData(new TimerHandleData));
    return h;
}

coro::awaitable<bool> ContextImpl::sleep(Handle timer,
        std::chrono::system_clock::time_point tp) {
    if (tp <= std::chrono::system_clock::now()) return true;
    return [this, timer, tp](coro::awaitable<bool>::result p) -> coro::prepared_coro{
        std::lock_guard _(_mx);
        auto iter = _handleMap.find(timer);
        if (iter == _handleMap.end() && iter->_value->get_type() != HandleType::timer) return p(false);
        auto tm = static_cast<TimerHandleData *>(iter->_value.get());
        auto r =  tm->sleep_until(tp, std::move(p));
        update_timeout(*tm);
        return r;
    };
}

coro::awaitable<ContextImpl::Handle> ContextImpl::accept(Handle server, std::chrono::system_clock::time_point timeout) {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(server);
    if (iter == _handleMap.end() || iter->_value->get_type() != HandleType::server) return null_handle;
    auto *srv = static_cast<ServerHandleData *>(iter->_value.get());
    int socket = srv->do_accept_sync();
    if (socket>=0) {
        return create_stream(socket);
    } else {
        return [this, server, timeout](coro::awaitable<Handle>::result p) -> coro::prepared_coro {
            std::lock_guard _(_mx);
            auto iter = _handleMap.find(server);
            auto &srv = *static_cast<ServerHandleData *>(iter->_value.get());
            auto r =  srv.do_accept_async(timeout, std::move(p));
            update_epoll_flags(server, srv);
            update_timeout( srv);
            return r;
        };
    }

}

coro::awaitable<std::size_t> ContextImpl::receive(Handle stream, char *buffer,
        std::size_t sz, std::chrono::system_clock::time_point timeout) {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(stream);
    if (iter == _handleMap.end()) return 0;
    return iter->_value->visit([&](auto &strm) -> coro::awaitable<std::size_t> {
        using T = std::decay_t<decltype(strm)> ;
        if constexpr(std::is_base_of_v<StreamHandleTag, T>) {
            int r = strm.recv_sync(buffer, sz);
            if (r >= 0) {
                return static_cast<std::size_t>(r);
            } else {
                return [this, stream, timeout](coro::awaitable<std::size_t>::result p) {
                    std::lock_guard _(_mx);
                    auto iter = _handleMap.find(stream);
                    auto &strm = *static_cast<T *>(iter->_value.get());
                    auto r = strm.recv_async(timeout, std::move(p));
                    update_epoll_flags(stream, strm);
                    update_timeout(strm);
                    return r;
                };
            }
        } else {
            return 0;
        }
    });
}

coro::awaitable<bool> ContextImpl::send(Handle stream, const char *buffer,
        std::size_t sz, std::chrono::system_clock::time_point timeout) {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(stream);
    if (iter == _handleMap.end()) return false;
    return iter->_value->visit([&](auto &strm)->coro::awaitable<bool> {
        using T = std::decay_t<decltype(strm)> ;
        if constexpr(std::is_base_of_v<StreamHandleTag, T>) {
            int r = strm.send_sync(buffer, sz);
            if (r > 0) {
                return true;
            } else if (r == 0) {
                return false;
            } else {
                return [this, stream, timeout](coro::awaitable<bool>::result p) {
                    std::lock_guard _(_mx);
                    auto iter = _handleMap.find(stream);
                    auto &strm = *static_cast<T *>(iter->_value.get());
                    auto r =  strm.send_async(timeout, std::move(p));
                    update_epoll_flags(stream, strm);
                    update_timeout(strm);
                    return r;
                };
            }
        } else {
            return false;
        }
    });
}

void ContextImpl::update_timeout(const AbstractHandleData &hd) {
    hd.visit([&](const auto &hd) {
        auto tm = hd.get_timeout();
        if (tm < _new_tp) {
            _new_tp = tm;
            _epoll_wk.set();
        }
    });
}

coro::awaitable<bool> ContextImpl::send_eof(Handle stream) {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(stream);
    if (iter == _handleMap.end()) return false;
    return iter->_value->visit([&](auto &strm)->coro::awaitable<bool> {
        using T = std::decay_t<decltype(strm)> ;
        if constexpr(std::is_base_of_v<StreamHandleTag, T>) {
            strm.send_close();
            return true;
        } else {
            return false;
        }
    });
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
            if (ipv4->sin_addr.s_addr != INADDR_ANY) {
                inet_ntop(AF_INET, &(ipv4->sin_addr), ip_str, INET_ADDRSTRLEN);
            } else {
                strcpy(ip_str, "127.0.0.1");
            }
            return std::string(ip_str) + ":" + std::to_string(ntohs(ipv4->sin_port));
        }

        case AF_INET6: {
            const sockaddr_in6* ipv6 = reinterpret_cast<const sockaddr_in6*>(addr);
            char ip_str[INET6_ADDRSTRLEN];
            if (IN6_IS_ADDR_UNSPECIFIED(&ipv6->sin6_addr)) {
                strcpy(ip_str, "::1");
            } else {
                inet_ntop(AF_INET6, &(ipv6->sin6_addr), ip_str, INET6_ADDRSTRLEN);
            }
            return "[" + std::string(ip_str) + "]:" + std::to_string(ntohs(ipv6->sin6_port));
        }

        case AF_UNIX: {
            const sockaddr_un* un = reinterpret_cast<const sockaddr_un*>(addr);
            return "unix:" + std::string(un->sun_path);
        }

        default:
            return "unknown";
    }
}

std::string SocketHandleData::get_host() const {
    sockaddr_storage sock_stor;
    socklen_t slen = sizeof(sock_stor);
    if (getsockname(_socket, reinterpret_cast<sockaddr *>(&sock_stor), &slen) == 0) {
        return sockaddrToString(reinterpret_cast<sockaddr *>(&sock_stor));
    } else {
        return {};
    }
}

std::string ContextImpl::get_host(Handle h) const {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(h);
    if (iter == _handleMap.end()) return {};
    return iter->_value->visit([&](const auto &p) -> std::string {
        using T = std::decay_t<decltype(p)>;
       if constexpr(std::is_base_of_v<SocketHandleData, T>) {
           return p.get_host();
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

std::shared_ptr<struct addrinfo> resolve_unix_socket(std::string_view host, bool passive) {
    std::vector<char> buff;
    buff.resize(host.size()+1);
    *std::copy(host.begin(), host.end(), buff.begin()) = '\0';
    char *path = buff.data();
    char *sep = strrchr(path, ':');
    int rights = 0;
    if (sep != nullptr && passive) {
        char *end;
        rights = static_cast<int>(strtoul(sep+1,&end,8));
        if (*end) throw std::invalid_argument("Invalid chmod part in unix socket spec");
        *sep = 0;
    }

    char *abspath = realpath(path, NULL);
    sockaddr_un sun = {};

    auto len = strlen(abspath);
    if (len >= sizeof (sun.sun_path)) {
        free(abspath);
        throw std::invalid_argument("Socket path is too long");
    }

    if (passive && !is_socket_active(path)) {
        unlink(abspath);
    }

    sun.sun_family = AF_UNIX;
    strncpy(sun.sun_path, path, sizeof(sun.sun_path)-1);
    free(abspath);

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
        if (vhost.substr(0,5) == "unix:") {
            return resolve_unix_socket(std::string_view(host).substr(5), passive);
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
        std::lock_guard _(_mx);
        Handle h = _handleMap.insert(PHandleData(new ServerHandleData(s,this)));
        _epoll.add(s, 0, h);
        return h;
    } catch (...) {
        close(s);
        throw;
    }
}

TwoPipesStreamData::TwoPipesStreamData(int in_fd, int out_fd, pid_t pid)
    :AbstractHandleData(HandleType::two_pipes)
    ,_in_fd(in_fd),_out_fd(out_fd),_pid(pid) {
    if (pid >= 0) {
        ChildManagerSingleton::getInstance().reg(pid, this);
        //we can ignore return value as there is no awaiter yet
    }

}

TwoPipesStreamData::~TwoPipesStreamData() {
    if (_in_fd < 0) ::close(_in_fd);
    if (_out_fd < 0) ::close(_out_fd);
    if (_pid >= 0) {
        ChildManagerSingleton::getInstance().unreg(_pid);
    }
}

int TwoPipesStreamData::recv_sync(char *buffer, std::size_t sz) {
    _recv_buffer = buffer;
    _recv_buffer_size = sz;
    return do_recv();
}
int TwoPipesStreamData::send_sync(const char *buffer, std::size_t sz) {
    _send_buffer = buffer;
    _send_buffer_size = sz;
    return do_send();
}
int TwoPipesStreamData::do_recv() {
    int r = ::read(_in_fd, _recv_buffer, _recv_buffer_size);
    if (r < 0) {
        int e = errno;
        if (e == EPIPE) r = 0;
        else if (e != EWOULDBLOCK) throw std::system_error(e, std::system_category(), "read");
    }
    return r;
}

bool TwoPipesStreamData::terminate_process() {
    if (_pid >= 0) {
        int r = ::kill(_pid, SIGKILL);
        if (r != -1) return true;
    }
    return false;
}

coro::prepared_coro TwoPipesStreamData::on_status_available(int status) {
    if (WIFEXITED(status)) {
        _process_status = WEXITSTATUS(status);
    } else {
        _process_status = -WTERMSIG(status);
    }
    return _process_status_promise(*_process_status);
}

std::optional<int> TwoPipesStreamData::get_pid_status_sync() {
    return _process_status;
}

coro::prepared_coro TwoPipesStreamData::get_pid_status_async(
    std::chrono::system_clock::time_point tp,
    coro::awaitable<int>::result p)
{
    if (_process_status.has_value()) return p(*_process_status);
    _process_status_promise = std::move(p);
    _pidstat_timeout = tp;
    return {};
}

int TwoPipesStreamData::do_send() {
    if (_send_closed) return 0;
    while (this->_send_buffer_size) {
        int r = ::write(_out_fd, _send_buffer, _send_buffer_size);
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

coro::prepared_coro TwoPipesStreamData::recv_async(std::chrono::system_clock::time_point tp, coro::awaitable<std::size_t>::result p) {
    if (_was_shutdown) return p(0);
    _recv_result = std::move(p);
    _recv_timeout = tp;
    return {};
}

coro::prepared_coro TwoPipesStreamData::send_async(std::chrono::system_clock::time_point tp, coro::awaitable<bool>::result p) {
    if (_was_shutdown) return p(false);
    _send_result = std::move(p);
    _send_timeout = tp;
    return {};
}

TwoCoros TwoPipesStreamData::on_complete(int flags) {
    TwoCoros out;
    if (flags & EPOLLIN) {
        try {

            if (_recv_result) {
                int r = do_recv();
                if (r >= 0) {
                    out.a = _recv_result.set_value(r);
                    _recv_timeout = max_timeout;
                }
            }

        } catch (...) {
            out.a = _recv_result.set_exception(std::current_exception());
        }
    }
    if (flags & EPOLLHUP) {
        if (_recv_result) {
            out.a = _recv_result.set_value(0);
        }
        if (_send_result) {
            out.b = _send_result.set_value(false);
        }
    }
    if (flags & EPOLLOUT) {
        try {

            if (_send_result) {
                int r = do_send();
                if (r >= 0) {
                    out.b = _send_result.set_value(r);
                    _send_timeout = max_timeout;
                }
            }

        } catch (...) {
            out.b = _send_result.set_exception(std::current_exception());
        }
    }
    return out;
}

TwoCoros TwoPipesStreamData::on_timeout(std::chrono::system_clock::time_point tp) {
    TwoCoros out;
    if (tp >= _recv_timeout) {
        out.a = _recv_result.set_empty();
        _recv_timeout =max_timeout;
    }
    if (tp >= _send_timeout) {
        out.b = _send_result.set_empty();
        _send_timeout =max_timeout;
    }
    if (!out.a && tp >= _pidstat_timeout) {
        out.a = _process_status_promise.set_empty();
        _pidstat_timeout = max_timeout;
    }
    return out;
}

TwoCoros TwoPipesStreamData::on_shutdown() {
    TwoCoros out;
    _was_shutdown = true;
    out.a = _recv_result.set_value(0);
    out.b = _send_result.set_value(false);
    _recv_timeout = max_timeout;
    _send_timeout = max_timeout;
    return out;
}

StreamState TwoPipesStreamData::get_state() const {
    if (_state_eof) return StreamState::closed;
    if (_send_closed) return StreamState::closing;
    return StreamState::active;
}

void TwoPipesStreamData::send_close() {
    if (!_send_closed) {
        _send_closed = true;
        if (_out_fd >= 0) {
            _out_fd = close(_out_fd);
            _out_fd = -1;
        }
    }
}

template<std::invocable<int, int> Svc>
void TwoPipesStreamData::apply_epoll_flags(Svc &&svc) const {
    if (_out_fd >= 0 && _send_result) {
        svc(_out_fd, EPOLLOUT|EPOLLONESHOT);
    }
    if (_in_fd >= 0 && _recv_result) {
        svc(_in_fd, EPOLLIN|EPOLLONESHOT);
    }
}

std::chrono::system_clock::time_point TwoPipesStreamData::get_timeout() const {
    return std::min(std::min(_send_timeout, _recv_timeout),_pidstat_timeout);
}



ContextImpl::Handle ContextImpl::connect(std::string host, std::string def_port) {
    if (host.compare(0, 7, "fifo-r:") == 0) {
        return connect_fifo(host.c_str()+7, O_RDONLY);
    }
    if (host.compare(0, 7, "fifo-w:") == 0) {
        return connect_fifo(host.c_str()+7, O_WRONLY);
    }

    auto a = dns_resolve(host, def_port, false);
    if (a == nullptr) throw std::system_error(ENOENT, std::system_category(), "DNS resolv failed");
    int s = socket(a->ai_family,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, a->ai_protocol);
    if (s < 0) throw std::system_error(errno, std::system_category(), "socket");
    try {
        int r = ::connect(s, a->ai_addr, a->ai_addrlen);
        if (r < 0) {
            int e = errno;
            if (e != EWOULDBLOCK && e != EINPROGRESS) {
                throw std::system_error(e, std::system_category(), "connect");
            }
        }
        std::lock_guard _(_mx);
        return create_stream(s);
    } catch (...) {
        close(s);
        throw;
    }
}

ContextImpl::Handle ContextImpl::create_stream(int s) {
    Handle h = _handleMap.insert(PHandleData(new StreamHandleData(s)));
    _epoll.add(s, EPOLLOUT|EPOLLONESHOT, h);
    return h;
}

StreamHandleData::StreamHandleData(int fd): SocketHandleData(HandleType::socket,fd), _opening(true) {
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
    if (r == 0) _state_eof = true;
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
        int r;
        r = ::send(_socket, this->_send_buffer, this->_send_buffer_size, MSG_DONTWAIT);
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
    if (_was_shutdown) return p(0);
    this->_recv_timeout = tp;
    this->_recv_result = std::move(p);
    return {};

}


coro::prepared_coro StreamHandleData::send_async(
                        std::chrono::system_clock::time_point tp,
                        coro::awaitable<bool>::result p) {

    if (_was_shutdown) return p(false);
    this->_send_timeout = tp;
    this->_send_result = std::move(p);
    return {};

}


TwoCoros StreamHandleData::on_complete(int flags) {
    TwoCoros out;
    if (flags & EPOLLIN) {
        try {

            if (_recv_result) {
                int r = do_recv();
                if (r >= 0) {
                    out.a = _recv_result.set_value(r);
                    _recv_timeout = max_timeout;
                }
            }

        } catch (...) {
            out.a = _recv_result.set_exception(std::current_exception());
        }
    }
    if (flags & EPOLLOUT) {
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
                    out.b = _send_result.set_value(r);
                    _send_timeout = max_timeout;
                }
            }

        } catch (...) {
            out.b = _send_result.set_exception(std::current_exception());
        }
    }
    return out;
}


TwoCoros StreamHandleData::on_timeout(std::chrono::system_clock::time_point tp) {
    TwoCoros out;
    if (tp >= _recv_timeout) {
        out.a = _recv_result.set_empty();
        _recv_timeout =max_timeout;
    }
    if (tp >= _send_timeout) {
        out.b = _send_result.set_empty();
        _send_timeout =max_timeout;
    }
    return out;
}

template<std::invocable<int, int> Svc>
void StreamHandleData::apply_epoll_flags(Svc &&svc) const {
    int flags = 0;
    if (_send_result) flags |= EPOLLOUT|EPOLLONESHOT;
    if (_recv_result) flags |= EPOLLIN|EPOLLONESHOT;
    svc(_socket, flags);

}


TwoCoros StreamHandleData::on_shutdown() {
    TwoCoros out;
    _was_shutdown = true;
    out.a = _recv_result.set_value(0);
    out.b = _send_result.set_value(false);
    _recv_timeout = max_timeout;
    _send_timeout = max_timeout;
    return out;
}


StreamState StreamHandleData::get_state() const {
    if (_connect_error || _state_eof) return StreamState::closed;
    if (_send_closed) return StreamState::closing;
    if (_opening) return StreamState::opening;
    return StreamState::active;
}


std::chrono::system_clock::time_point StreamHandleData::get_timeout() const {
    return std::min(_send_timeout, _recv_timeout);
}


void StreamHandleData::send_close() {
    if (!_send_closed) {
        _send_closed = true;
        int r = ::shutdown(_socket, SHUT_WR);
        if (r == -1) {
            throw std::system_error(errno, std::system_category(), "socket shutdown (send_eof)");
        }
    }
}


void ContextImpl::close(Handle h) {
    TwoCoros r;
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(h);
    if (iter != _handleMap.end()) {
        r = iter->_value->visit([&](auto &p) -> TwoCoros {
           TwoCoros r =  p.on_shutdown();
           p.apply_epoll_flags([&](int socket, int){
              _epoll.del(socket);
           });
           return r;
        });
        _handleMap.erase(iter);
    }
}



Environment Environment::current() {
    char **env = environ;
    Environment env_vars;
      while (*env != nullptr) {
          std::string entry(*env);
          size_t pos = entry.find('=');
          if (pos != std::string::npos) {
              std::string key = entry.substr(0, pos);
              std::string value = entry.substr(pos + 1);
              env_vars[key] = value;
          }
          env++;
      }
      return env_vars;
}

ContextImpl::Handle ContextImpl::connect_process(std::string_view path, std::span<const std::string_view> argv,  const Environment & env) {

    std::size_t needsz = std::accumulate(argv.begin(), argv.end(), path.size()+1, [](std::size_t a, const std::string_view &x){
        return a + x.size()+1;
    })+ std::accumulate(env.begin(), env.end(), std::size_t(0), [](std::size_t a, const auto &x){
        return a + x.first.size() + x.second.size() + 2;
    });
    std::size_t entry_count = 3 + argv.size() + env.size(); //arg0+argv.size()+env.size()+2x null)
    char **table = reinterpret_cast<char **>(::malloc(sizeof(char **)*entry_count+sizeof(char *)*needsz));
    char *strings = reinterpret_cast<char *>(table+entry_count);
    char **table_p = table;
    char **env_table = nullptr;
    char *str_p = strings;
    *table_p++ = str_p;
    str_p = std::copy(path.begin(), path.end(), str_p);
    *str_p++ = 0;
    for (const auto &s: argv) {
        *table_p++ = str_p;
        str_p = std::copy(s.begin(), s.end(), str_p);
        *str_p++ = 0;
    }
    *table_p++ = nullptr;
    if (env.empty()) {
        env_table = environ;
    } else {
        env_table = table_p;
        for (const auto &[k,v]: env) {
            if (k.empty() || k.find('=') != k.npos) continue;
            *table_p++ = str_p;
            str_p = std::copy(k.begin(), k.end(), str_p);
            *str_p++='=';
            str_p = std::copy(v.begin(), v.end(), str_p);
            *str_p++ = 0;
        }
        *table_p++ = nullptr;
    }


    pid_t pid;
    int pipe_parent_to_child[2] = {-1,-1};
    int pipe_child_to_parent[2] = {-1,-1};
    try {
        if (pipe2(pipe_parent_to_child, O_CLOEXEC|O_NONBLOCK) == -1)
            throw std::system_error(errno, std::system_category(), "pipe parent->child");
        if (pipe2(pipe_child_to_parent, O_CLOEXEC|O_NONBLOCK) == -1)
            throw std::system_error(errno, std::system_category(), "pipe child->parent");

         posix_spawn_file_actions_t actions;
         posix_spawn_file_actions_init(&actions);

         posix_spawn_file_actions_adddup2(&actions, pipe_parent_to_child[0], STDIN_FILENO);
         posix_spawn_file_actions_addclose(&actions, pipe_parent_to_child[1]);
         posix_spawn_file_actions_addclose(&actions, pipe_child_to_parent[0]);

         posix_spawn_file_actions_adddup2(&actions, pipe_child_to_parent[1], STDOUT_FILENO);
         posix_spawn_file_actions_addclose(&actions, pipe_child_to_parent[0]);
         posix_spawn_file_actions_addclose(&actions, pipe_parent_to_child[1]);


         // Spuštění procesu
        if (posix_spawn(&pid, table[0], &actions, NULL, table, env_table) != 0) {
            throw std::system_error(errno, std::system_category(), std::string("posix_spawn:").append(path));
        }

        ::close(pipe_parent_to_child[0]);
        ::close(pipe_child_to_parent[1]);

        ::free(table);
        return create_from_handles(pipe_child_to_parent[0], pipe_parent_to_child[1], pid);

    } catch (...) {
        if (pipe_parent_to_child[0] >= 0) ::close(pipe_parent_to_child[0]);
        if (pipe_parent_to_child[1] >= 0) ::close(pipe_parent_to_child[1]);
        if (pipe_child_to_parent[0] >= 0) ::close(pipe_child_to_parent[0]);
        if (pipe_child_to_parent[1] >= 0) ::close(pipe_child_to_parent[1]);
        free(table);
        throw;
    }
}
ContextImpl::Handle ContextImpl::connect_stdinout() {
    int rd_fd = -1;
    int wr_fd = -1;
    try {
        rd_fd = eventfd(0,O_CLOEXEC);
        if (rd_fd == -1) {
            throw std::system_error(errno, std::system_category(), "cannot reserve descriptor by creating eventfd");
        }

        wr_fd = eventfd(0,O_CLOEXEC);
        if (wr_fd == -1) {
            throw std::system_error(errno, std::system_category(), "cannot reserve descriptor by creating eventfd");
        }
        int r = dup3(STDIN_FILENO, rd_fd, O_CLOEXEC);
        if (r == -1) {
            throw std::system_error(errno, std::system_category(), "dup3 (rd)");
        }
        r = dup3(STDOUT_FILENO, wr_fd, O_CLOEXEC);
        if (r == -1) {
            throw std::system_error(errno, std::system_category(), "dup3 (wr)");
        }

        ::close(STDIN_FILENO);
        ::close(STDOUT_FILENO);

        fcntl(rd_fd, F_SETFL, fcntl(rd_fd, F_GETFL) | O_NONBLOCK);
        fcntl(wr_fd, F_SETFL, fcntl(wr_fd, F_GETFL) | O_NONBLOCK);

        return create_from_handles(rd_fd, wr_fd,-1);

    } catch (...) {
        if (rd_fd >=0 ) ::close(rd_fd);
        if (wr_fd >=0 ) ::close(wr_fd);
        throw;
    }
}

ContextImpl::Handle ContextImpl::create_from_handles(int rd_fd, int wr_fd, pid_t pid) {
    std::lock_guard _(_mx);
    Handle h  = _handleMap.emplace(PHandleData(new TwoPipesStreamData(rd_fd, wr_fd, pid)));
    _epoll.add(rd_fd,0, h);
    _epoll.add(wr_fd,0, h);
    if (pid >= 0 && !_child_monitor) {
        ChildManagerSingleton &m = ChildManagerSingleton::getInstance();
        _child_monitor = _handleMap.emplace(PHandleData(new SigHandleData(SigHandleData::schild)));
        _epoll.add(m.get_fd(), EPOLLIN, _child_monitor);

    }
    return h;

}

coro::awaitable<BreakType> ContextImpl::wait_on_break() {
    return [this](coro::awaitable<BreakType>::result r) {
        std::lock_guard _(_mx);
        BreakManagerSingleton &m = BreakManagerSingleton::getInstance();
        if (_break_monitor == null_handle) {
            _break_monitor = _handleMap.emplace(PHandleData(new SigHandleData(SigHandleData::sbreak)));
            _epoll.add(m.get_fd(), EPOLLIN, _break_monitor);
        }
        m.reg(std::move(r));
    };
}

bool ContextImpl::terminate_process(Handle h) {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(h);
    if (iter != _handleMap.end()) {
        return iter->_value->visit([&](auto &p)  {
            using T = std::decay_t<decltype(p)>;
            if constexpr(std::is_same_v<T, TwoPipesStreamData>) {
                return p.terminate_process();
            } else {
                return false;
            }
        });

    }
    return false;

}

coro::awaitable<int> ContextImpl::get_process_exit_status(Handle h, std::chrono::system_clock::time_point tp) {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(h);
    if (iter != _handleMap.end()) {
        return iter->_value->visit([&](auto &p) -> coro::awaitable<int> {
            using T = std::decay_t<decltype(p)>;
            if constexpr(std::is_same_v<T, TwoPipesStreamData>) {
                auto s= p.get_pid_status_sync();
                if (s.has_value()) return *s;
                else return [this, h, tp](coro::awaitable<int>::result r) {
                    std::lock_guard _(_mx);
                    auto iter = _handleMap.find(h);
                    if (iter != _handleMap.end()) {
                        auto &p = static_cast<T &>(*iter->_value);
                        return p.get_pid_status_async(tp, std::move(r));
                    } else {
                        return r(-1);
                    }
                };
            } else {
                return false;
            }
        });

    }
    return -1;
}

ContextImpl::Handle ContextImpl::connect_fifo(const char *fname, int flags) {
    int fd = ::open(fname, O_CLOEXEC|O_NONBLOCK|flags);
    if (fd < 0) throw std::system_error(errno, std::system_category(), "Unable to open fifo");
    if (flags == O_RDONLY) {
        return create_from_handles(fd, -1,-1);
    } else if (flags == O_WRONLY) {
        return create_from_handles(-1, fd,-1);
    } else {
        ::close(fd);
        throw std::invalid_argument("connect_fifo: invalid flags");
    }
}

ChildManagerSingleton &ChildManagerSingleton::getInstance() {
    static ChildManagerSingleton inst;
    return inst;
}

ChildManagerSingleton::ChildManagerSingleton() {

    _eventfd = eventfd(0, O_CLOEXEC|O_NONBLOCK);
    if (_eventfd < 0) {
        throw std::system_error(errno, std::system_category(), "eventfd (child monitor)");
    }

    struct sigaction sa{};
    sa.sa_handler = ChildManagerSingleton::handleSigChld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &sa, nullptr) == -1) {
        throw std::system_error(errno, std::system_category(), "sigaction SIGCHILD");
    }
}

void ChildManagerSingleton::handleSigChld(int) {

    ChildManagerSingleton &inst = getInstance();
    eventfd_write(inst._eventfd, 1);
}

ChildManagerSingleton::~ChildManagerSingleton() {
    ::close(_eventfd);
}

coro::prepared_coro ChildManagerSingleton::on_event() {
    eventfd_t val;
    coro::prepared_coro r;
    eventfd_read(_eventfd, &val);

    auto iter = std::remove_if(_pidmap.begin(),
            _pidmap.end(), [&](const auto &p) {
        if (!r) {
            int status = 0;
            int e = waitpid(p.first, &status, WNOHANG);
            if (e > 0) {
                if (p.second) {
                    r = p.second->on_status_available(status);
                }
                return true;
            }
        }
        return false;
    });
    _pidmap.erase(iter, _pidmap.end());
   return r;
}


coro::prepared_coro ChildManagerSingleton::reg(pid_t p, TwoPipesStreamData *owner) {
    std::lock_guard _(_mx);
    int status;
    int e = waitpid(p, &status, WNOHANG);
    if (e > 0) {
        return owner->on_status_available(status);
    }
    _pidmap.emplace_back(p, owner);
    return {};
}
void ChildManagerSingleton::unreg(pid_t p) {
    std::lock_guard _(_mx);
    for (auto &[wp,o]: _pidmap) {
        if (wp == p) o = nullptr;
    }
}

TwoCoros SigHandleData::on_complete(int) {
    switch (_sigtype) {
        case schild: return ChildManagerSingleton::getInstance().on_event();
        case sbreak: return BreakManagerSingleton::getInstance().on_event();
        default: break;
    }
}

SigHandleData::SigHandleData(SigType sigtype): AbstractHandleData(HandleType::signalfd),_sigtype(sigtype) {}



BreakManagerSingleton::BreakManagerSingleton() {

    _eventfd = eventfd(0, O_CLOEXEC|O_NONBLOCK);
    if (_eventfd < 0) {
        throw std::system_error(errno, std::system_category(), "eventfd (child monitor)");
    }

    struct sigaction sa{};
    sa.sa_handler = BreakManagerSingleton::handleSig;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART ;

    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        throw std::system_error(errno, std::system_category(), "sigaction SIGINT");
    }
    if (sigaction(SIGTERM, &sa, nullptr) == -1) {
        throw std::system_error(errno, std::system_category(), "sigaction SIGTERM");
    }
    if (sigaction(SIGQUIT, &sa, nullptr) == -1) {
        throw std::system_error(errno, std::system_category(), "sigaction SIGQUIT");
    }
    if (sigaction(SIGHUP, &sa, nullptr) == -1) {
        throw std::system_error(errno, std::system_category(), "sigaction SIGHUP");
    }

}

BreakManagerSingleton::~BreakManagerSingleton() {
    ::close(_eventfd);
}

BreakManagerSingleton& BreakManagerSingleton::getInstance() {
    static BreakManagerSingleton inst;
    return inst;
}

struct MultiPrepared : coro::coro_frame<MultiPrepared> {
    std::vector<coro::prepared_coro> lst;

    void do_resume() {
        delete this;
    }
    void do_destroy() {
        delete this;
    }
};

TwoCoros BreakManagerSingleton::on_event() {
    TwoCoros out;

    std::lock_guard _(_mx);
    eventfd_t val;
    int s = std::exchange(_signal,0);
    eventfd_read(_eventfd, &val);
    if (_awts.size() == 0) {
        for (int s: {SIGINT, SIGTERM, SIGQUIT, SIGHUP}) signal(s, SIG_DFL);
        raise(s);
        abort();
    }
    BreakType br;
    switch (s) {
        case SIGINT: br = BreakType::interrupt;break;
        case SIGTERM: br = BreakType::terminate;break;
        case SIGQUIT: br = BreakType::quit;break;
        case SIGHUP: br = BreakType::terminal_close;break;
        default: return {};
    }
    if (_awts.size() == 1) {
        out.a = _awts[0](br);
        _awts.clear();
    } else  if (_awts.size() == 2) {
        out.a = _awts[0](br);
        out.b = _awts[1](br);
        _awts.clear();
    } else {
        auto m = std::make_unique<MultiPrepared>();
        m->lst.reserve(_awts.size());
        for (auto &x: _awts) m->lst.push_back(x(br));
        _awts.clear();
        out.a = m.release()->create_handle();
    }
    return out;
}

void BreakManagerSingleton::reg(coro::awaitable<BreakType>::result r) {
    std::lock_guard _(_mx);
    _awts.push_back(std::move(r));
}

void BreakManagerSingleton::handleSig(int signo) {
    BreakManagerSingleton &inst = getInstance();
    inst._signal = signo;
    eventfd_write(inst._eventfd, 1);
}

}
