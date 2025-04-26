#include "win_mswsex.hpp"
#include "context_windows.hpp"
#include "context_inc.hpp"
#include "process.hpp"
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

static MsWSock mswsock;

namespace coroserver {

LPOVERLAPPED init_ovr(LPOVERLAPPED ptr) {
    ZeroMemory(ptr,sizeof(OVERLAPPED));
    return ptr;
}


coro::prepared_coro TimerHandleData::sleep_until(std::chrono::system_clock::time_point tp, coro::awaitable<bool>::result p) {
    if (_was_shutdown) {
        return p(false);
    }
    _tp = tp;
    _p = std::move(p);
    return {};
}

coro::prepared_coro TimerHandleData::on_timeout(std::chrono::system_clock::time_point tp) {
    if (tp >= _tp) {
        _tp = std::chrono::system_clock::time_point::max();
        return _p(true);
    }
    return {};
}

coro::prepared_coro TimerHandleData::on_shutdown() {
    _was_shutdown = true;
    _tp = std::chrono::system_clock::time_point::max();
    return _p(false);
}

ServerHandleData::ServerHandleData(SOCKET s, int af, ContextImpl *ctx)
    :SocketHandleData(HandleType::server,s),_af(af),_ctx(ctx) {}

coro::prepared_coro ServerHandleData::do_accept_async(
            std::chrono::system_clock::time_point tp,
            coro::awaitable<Context::Handle>::result p) {

    if (_was_shutdown) return p.set_value(0);

    SOCKET newSocket = socket(_af, SOCK_STREAM, IPPROTO_TCP);  //create socket
    if (newSocket == INVALID_SOCKET) {
        return p.set_exception(std::make_exception_ptr(Win32Error("socket")));
    }
    _prepared_socket = newSocket;
    DWORD rd = 0;
    constexpr auto bfs = sizeof(_accept_buffer)/2;
    _p = std::move(p);
    _tp = tp;
    if (!mswsock.AcceptEx(_socket, _prepared_socket,  _accept_buffer, 0, bfs, bfs, &rd, init_ovr(&_ovr))) {
        auto err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            return p.set_exception(std::make_exception_ptr(Win32Error(err, "AcceptEx")));
        } else {
            return {};
        }
    }
    return on_complete(0, &_ovr);
}

coro::prepared_coro ServerHandleData::on_complete(DWORD, LPOVERLAPPED) {
    if (_prepared_socket == INVALID_SOCKET) return {};
    setsockopt(_prepared_socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<char *>(&_socket), sizeof(SOCKET));
    sockaddr_storage *local, *remote;
    int local_sz = sizeof(sockaddr_storage), remote_sz = sizeof(sockaddr_storage);
    constexpr auto bfs = sizeof(_accept_buffer)/2;
    mswsock.GetAcceptExSockaddrs(_accept_buffer,0,bfs,bfs,
        reinterpret_cast<sockaddr **>(&local), &local_sz, reinterpret_cast<sockaddr **>(&remote), &remote_sz);

    auto h = _ctx->create_stream(_prepared_socket);
    _prepared_socket = INVALID_SOCKET;
    _tp = _tp.max();
    return _p.set_value(h);
}

coro::prepared_coro ServerHandleData::on_error(DWORD err, LPOVERLAPPED ovr) {
    if (ovr == &_ovr) {
        _tp = _tp.max();
        if (err == ERROR_OPERATION_ABORTED) {
            if (_was_shutdown) {
                return _p.set_value(0);
            } else {
                return _p.set_empty();
            }
        } else {
            return _p.set_exception(std::make_exception_ptr(Win32Error(err, "Accept async error")));
        }
    }
    return {};
}

coro::prepared_coro ServerHandleData::on_timeout(std::chrono::system_clock::time_point tp) {
    if (_tp >= tp) {
        _tp = tp.max();
        CancelIoEx(reinterpret_cast<HANDLE>(_socket), &_ovr);
    }
    return {};
}

coro::prepared_coro ServerHandleData::on_shutdown() {
    if (!_was_shutdown) {
        _was_shutdown = true;
        if (_p) {
            CancelIoEx(reinterpret_cast<HANDLE>(_socket), &_ovr);
        }
    }
    return {};
}

bool ServerHandleData::safe_to_close() const {
    return _closing && !_p;
}

ServerHandleData::~ServerHandleData() {
    closesocket(_socket);
}

StreamHandleData::StreamHandleData(SOCKET h):SocketHandleData(HandleType::socket,h) {}

void StreamHandleData::set_recv_buffer(char *buffer, std::size_t sz) {
    _recv_buffer = buffer;
    _recv_buffer_size = sz;
}
void StreamHandleData::set_send_buffer(const char *buffer, std::size_t sz) {
    _send_buffer = buffer;
    _send_buffer_size = sz;
}


coro::prepared_coro StreamHandleData::recv_async(std::chrono::system_clock::time_point tp, coro::awaitable<std::size_t>::result p) {
    if (_connect_error) return p.set_exception(std::make_exception_ptr(Win32Error(_connect_error, "Connect error")));
    _recv_timeout = tp;
    _recv_result = std::move(p);
    DWORD bytes = 0;
    WSABUF buff = {static_cast<ULONG>(_recv_buffer_size), _recv_buffer};
    DWORD flags = 0;
    if (WSARecv(_socket,&buff,1,&bytes, &flags, init_ovr(&_recv_ovr), NULL)) {
        DWORD err = WSAGetLastError();
        if (err == WSAECONNRESET || err == WSAECONNABORTED) {
            _state_eof = true;
            return _recv_result.set_value(0);
        } else if (err == WSA_IO_PENDING) return {};
        else return _recv_result.set_exception(std::make_exception_ptr(Win32Error(err, "WSARecv")));
    }    
    return on_complete(bytes, &_recv_ovr);
}

coro::prepared_coro StreamHandleData::send_async(std::chrono::system_clock::time_point tp, coro::awaitable<bool>::result p) {
    if (_connect_error) return p.set_exception(std::make_exception_ptr(Win32Error(_connect_error, "Connect error")));
    _send_timeout = tp;
    _send_result = std::move(p);
    if (_opening) return {};
    DWORD bytes = 0;
    DWORD flags = 0;
    WSABUF buff = {static_cast<ULONG>(_send_buffer_size), const_cast<char *>(_send_buffer)};
    if (WSASend(_socket, &buff, 1, &bytes, flags, init_ovr(&_send_ovr), NULL)) {
        DWORD err = WSAGetLastError();
        if (err == WSA_IO_PENDING) return {};
        if (err == WSAECONNRESET || err == WSAECONNABORTED) return _send_result.set_value(false);
        return _send_result.set_exception(std::make_exception_ptr(Win32Error(err, "WSASend")));
    }    
    return on_complete(bytes, &_send_ovr);
}

coro::prepared_coro StreamHandleData::on_complete(DWORD bytes, LPOVERLAPPED ovr) {
    if (ovr == &_send_ovr) {
        if (_opening) {
            setsockopt(_socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
            _opening = false;
        }
        _send_buffer+=bytes;
        _send_buffer_size-= bytes;
        if (_send_buffer_size) {
            return send_async(_send_timeout, std::move(_send_result));
        }
        _send_timeout = _send_timeout.max();
        return _send_result(true);
    } else if (ovr == &_recv_ovr) {
        _recv_timeout = _send_timeout.max();
        if (bytes == 0) {
            _state_eof = true;
        }
        return _recv_result(bytes);
    }
    return {};
}

coro::prepared_coro StreamHandleData::on_timeout(std::chrono::system_clock::time_point tp) {
    if (tp >= _send_timeout) {
        CancelIoEx(reinterpret_cast<HANDLE>(_socket), &_send_ovr);
        _send_timeout = _send_timeout.max();
    }
    if (tp >= _recv_timeout) {
        CancelIoEx(reinterpret_cast<HANDLE>(_socket), &_recv_ovr);
        _recv_timeout = _recv_timeout.max();
    }
    return {};
}

coro::prepared_coro StreamHandleData::on_shutdown() {
    _was_shutdown = true;
    CancelIoEx(reinterpret_cast<HANDLE>(_socket), &_send_ovr);
    CancelIoEx(reinterpret_cast<HANDLE>(_socket), &_recv_ovr);
    _send_timeout = _send_timeout.max();
    _recv_timeout = _recv_timeout.max();
    return {};
}

coro::prepared_coro StreamHandleData::on_error(DWORD error, LPOVERLAPPED ovr) {
    if (ovr == &_send_ovr) {
        _send_timeout = _send_timeout.max();
        if (_opening) {
            _connect_error = error;
            return _send_result.set_exception(std::make_exception_ptr(Win32Error(error, "Connect Error")));
        }
        if (error == WSA_OPERATION_ABORTED || error == WSAECONNABORTED || error == WSAECONNRESET) {
            return _send_result.set_value(false);
        }
        return _send_result.set_exception(std::make_exception_ptr(Win32Error(error, "Async send")));
    } else if (ovr == &_recv_ovr) {
        _recv_timeout = _recv_timeout.max();
        if (error == WSA_OPERATION_ABORTED) {
            if (_was_shutdown) return _recv_result.set_value(0);
            else return _recv_result.set_empty();
        }
        if (error == WSAECONNABORTED || error == WSAECONNRESET) {
            return _recv_result.set_value(0);
        }
        return _recv_result.set_exception(std::make_exception_ptr(Win32Error(error, "Async recv")));
    }
    return {};

}

StreamState StreamHandleData::get_state() const {
    if (_opening) return StreamState::opening;
    if (_state_eof) return StreamState::closed;
    if (_send_closed) return StreamState::closing;
    return StreamState::active;
}

void StreamHandleData::send_close() {
    if (!_send_closed) {
        _send_closed = true;
        ::shutdown(_socket, SD_SEND);
    }
}

std::chrono::system_clock::time_point StreamHandleData::get_timeout() const {
    return  std::min(_send_timeout, _recv_timeout);
}

bool StreamHandleData::safe_to_close() const {
    return _closing && !_send_result && !_recv_result;
}
StreamHandleData::~StreamHandleData() {
    closesocket(_socket);
}


ContextImpl::ContextImpl() {}


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
                hm._value->visit([&](auto &p) {
                    auto ctp = p.get_timeout();
                    if (ctp <= now) {
                            auto r = p.on_timeout(now);
                            if (r) prepared.push_back(std::move(r));
                    } else {
                        if (ctp < tp) tp = ctp;
                    }
                });
            }
            _awaiting_tp = tp;
        }
        lk.unlock();
        if (prepared.empty()) {
            auto ev =  _iocp.wait(_awaiting_tp);
            if (ev.error != WAIT_TIMEOUT) {
                lk.lock();
                Handle h = ev.key;
                if (h != null_handle) {
                    auto iter = _handleMap.find(h);
                    if (iter != _handleMap.end()) {
                        auto p = iter->_value->visit([&](auto &item) -> coro::prepared_coro {
                            coro::prepared_coro p;
                            if (ev.error) {
                                p = item.on_error(ev.error, ev.overlapped);
                            } else {
                                p = item.on_complete(ev.bytes, ev.overlapped);
                            }
                            if (item.safe_to_close()) {
                                _handleMap.erase(iter);
                            }
                            return p;
                        });
                        if (p) prepared.push_back(std::move(p));
                    }
                }
                lk.unlock();
            }
        }
        prepared.clear();   //execute prepared
        lk.lock();
    }
    _iocp.post(0);
}


void ContextImpl::signal_stop() {
    std::lock_guard _(_mx);
    _stop_signaled = true;
    _iocp.post(0);
}

StreamState ContextImpl::get_state(Handle h) {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(h);
    if (iter == _handleMap.end()) return StreamState::closed;
    return iter->_value->visit([](const auto &x){return x.get_state();});
}

void ContextImpl::shutdown(Handle h) {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(h);
    if (iter == _handleMap.end()) return;
    iter->_value->visit([](auto &x){return x.on_shutdown();});
}

void ContextImpl::close(Handle h) {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(h);
    if (iter == _handleMap.end()) return;
    iter->_value->visit([&](auto &x){
        x.on_shutdown();
        x.set_closing();
        if (x.safe_to_close()) {
            _handleMap.erase(iter);
        }
    });
}

ContextImpl::Handle ContextImpl::create_timer() {
    return _handleMap.insert(PHandleData(new TimerHandleData));
}

coro::awaitable<bool> ContextImpl::sleep(Handle timer, std::chrono::system_clock::time_point tp) {
    return [this, timer, tp](coro::awaitable<bool>::result p) -> coro::prepared_coro {
        if (!p) return {};
        std::lock_guard _(_mx);
        auto iter = _handleMap.find(timer);
        if (iter == _handleMap.end()) return p(false);
        return iter->_value->visit([&]( auto &t)  {
            using T = std::decay_t<decltype(t)>;
            coro::prepared_coro pc;
            if constexpr(std::is_same_v<T, TimerHandleData>) {
                auto me = this;
                pc = t.sleep_until(tp, std::move(p));
                me->update_timeout(t);  //"this" can be destroyed here
            } else {
                pc =  p(false);
            }
            return pc;

        });
    };
}


std::shared_ptr<struct addrinfo> dns_resolve(std::string host, std::string def_port, bool passive) {
    if (!host.empty()) {
        std::string_view vhost = host;
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
    SOCKET s = socket(a->ai_family,SOCK_STREAM, a->ai_protocol);
    if (s < 0) throw std::system_error(errno, std::system_category(), "socket");
    try {
        if (bind(s, a->ai_addr, static_cast<int>(a->ai_addrlen)) != 0) {
            throw std::system_error(errno, std::system_category(), "bind");
        }
        if (listen(s, SOMAXCONN) != 0) {
            throw std::system_error(errno, std::system_category(), "listen");
        }
        std::lock_guard _(_mx);
        Handle h = _handleMap.insert(PHandleData(new ServerHandleData(s,a->ai_family,this)));
        _iocp.add(reinterpret_cast<HANDLE>(s), h);
        return h;
    } catch (...) {
        close(s);
        throw;
    }
}


coro::awaitable<ContextImpl::Handle> ContextImpl::accept(Handle server, std::chrono::system_clock::time_point timeout) {
    return [this, server, timeout](coro::awaitable<ContextImpl::Handle>::result r) -> coro::prepared_coro{
        if (!r) return {};
        std::lock_guard _(_mx);
        auto iter = _handleMap.find(server);
        if (iter == _handleMap.end()) return r(0);
        return iter->_value->visit([&](auto &srv) -> coro::prepared_coro {
            using T = std::decay_t<decltype(srv)>;
            coro::prepared_coro p;
            if constexpr(std::is_same_v<T, ServerHandleData>) {
                auto me =this;
                p =srv.do_accept_async(timeout, std::move(r));
                if (!p) me->update_timeout(srv); //"this" can be destroyed here
            } else {
                p = r(0);
            }
            return p;
        });
    };

}
coro::awaitable<size_t> ContextImpl::receive(Handle stream, char *buffer, std::size_t sz, std::chrono::system_clock::time_point timeout) {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(stream);
    if (iter == _handleMap.end()) return 0;
    return iter->_value->visit([&]( auto &strm) -> coro::awaitable<size_t> {
        using T = std::decay_t<decltype(strm)>;
        if constexpr(std::is_base_of_v<StreamHandleTag, T>) {
            strm.set_recv_buffer(buffer, sz);
            return [this, stream, timeout](coro::awaitable<size_t>::result p) {
                coro::prepared_coro pc;
                if (p) {
                    std::lock_guard _(_mx);
                    auto iter = _handleMap.find(stream);
                    if (iter == _handleMap.end()) pc = p(0);
                    else {
                        auto &strm = static_cast<T &>(*iter->_value);
                        auto me = this;
                        pc =  strm.recv_async(timeout, std::move(p));
                        if (!pc) me->update_timeout(strm); //"this" can be destroyed here
                    }
                }
                return pc;
            };
        } else {
            return 0;
        }
    });
}
coro::awaitable<bool> ContextImpl::send(Handle stream, const char *buffer, std::size_t sz, std::chrono::system_clock::time_point timeout) {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(stream);
    if (iter == _handleMap.end()) return 0;
    return iter->_value->visit([&]( auto &strm) -> coro::awaitable<bool> {
        using T = std::decay_t<decltype(strm)>;
        if constexpr(std::is_base_of_v<StreamHandleTag, T>) {
            strm.set_send_buffer(buffer, sz);
            return [this, stream, timeout](coro::awaitable<bool>::result p) {
                coro::prepared_coro pc;
                if (p) {
                    std::lock_guard _(_mx);
                    auto iter = _handleMap.find(stream);
                    if (iter == _handleMap.end()) pc = p(0);
                    else {
                        auto &strm = static_cast<T &>(*iter->_value);
                        auto me = this;
                        pc =  strm.send_async(timeout, std::move(p));
                        if (!pc) me->update_timeout(strm); //"this" can be destroyed here
                    }
                }
                return pc;
            };
        } else {
            return 0;
        }
    });

}
coro::awaitable<bool> ContextImpl::send_eof(Handle stream) {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(stream);
    if (iter == _handleMap.end()) return 0;
    return iter->_value->visit([&]( auto &strm) -> coro::awaitable<bool> {
        using T = std::decay_t<decltype(strm)>;
        if constexpr(std::is_base_of_v<StreamHandleTag, T>) {
            strm.send_close();
            update_timeout(strm);
            return true;
        } else {
            return false;
        }
    });
}

ContextImpl::Handle ContextImpl::connect(std::string host, std::string def_port) {
    auto a = dns_resolve(std::move(host), std::move(def_port), false);
    if (a == nullptr) throw std::system_error(ENOENT, std::system_category(), "DNS resolv failed");
    SOCKET s = socket(a->ai_family,a->ai_socktype, a->ai_protocol);
    if (s < 0) throw std::system_error(errno, std::system_category(), "socket");
    try {
        coro::prepared_coro pc;
        auto hds = std::make_unique<StreamHandleData>(s);
        auto &hdsr = *hds;
        hdsr.mark_opening();
        std::lock_guard _(_mx);
        Handle h = _handleMap.emplace(PHandleData(hds.release()));
        _iocp.add(reinterpret_cast<HANDLE>(s),h);

        {
            struct sockaddr_storage addr = {};
            ZeroMemory(&addr, sizeof(addr));
            addr.ss_family = static_cast<ADDRESS_FAMILY>(a->ai_family);
            bind(s, reinterpret_cast<SOCKADDR*>(&addr), static_cast<int>(a->ai_addrlen));
        }

        if (!mswsock.ConnectEx(s, a->ai_addr, static_cast<int>(a->ai_addrlen),NULL,0,NULL,init_ovr(hdsr.get_connect_overlapped()))) {
            DWORD error = WSAGetLastError();
            if (error != WSA_IO_PENDING) {
                _handleMap.erase(h);
                throw Win32Error(error, "ConnectEx");
            }
        }
        update_timeout(hdsr);
        return h;
    } catch (...) {
        closesocket(s);
        throw;
    }
}

ContextImpl::Handle ContextImpl::connect(SpecialDevice) {
    throw std::runtime_error("unsupported");
}

static std::string sockaddrToString(const sockaddr* addr) {
    if (addr == nullptr) {
        return "unknown";
    }

    switch (addr->sa_family) {
        case AF_INET: {
            const sockaddr_in* ipv4 = reinterpret_cast<const sockaddr_in*>(addr);
            char ip_str[INET_ADDRSTRLEN];
            if (ipv4->sin_addr.S_un.S_addr != INADDR_ANY) {
                inet_ntop(AF_INET, &(ipv4->sin_addr), ip_str, INET_ADDRSTRLEN);
            } else {
                strcpy_s(ip_str, "127.0.0.1");
            }
            return std::string(ip_str) + ":" + std::to_string(ntohs(ipv4->sin_port));
        }

        case AF_INET6: {
            const sockaddr_in6* ipv6 = reinterpret_cast<const sockaddr_in6*>(addr);
            char ip_str[INET6_ADDRSTRLEN];
            if (IN6_IS_ADDR_UNSPECIFIED(&ipv6->sin6_addr)) {
                strcpy_s(ip_str, "::1");
            } else {
                inet_ntop(AF_INET6, &(ipv6->sin6_addr), ip_str, INET6_ADDRSTRLEN);
            }
            return "[" + std::string(ip_str) + "]:" + std::to_string(ntohs(ipv6->sin6_port));
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
           SOCKET socket = p.get_socket();
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

void ContextImpl::update_timeout(const AbstractHandleData &hd) {
    hd.visit([&](const auto &hd) {
        auto tm = hd.get_timeout();
        if (tm < _new_tp) {
            _new_tp = tm;
            _iocp.post(0);
        }
    });
}

ContextImpl::Handle ContextImpl::create_stream(SOCKET socket) {
    Handle h = _handleMap.emplace(PHandleData(new StreamHandleData(socket)));
    _iocp.add(reinterpret_cast<HANDLE>(socket), h);
    return h;

}


Environment Environment::current() {
    LPWCH env = GetEnvironmentStringsW();
    std::vector<char> buff;
    LPWCH iter = env;
    Environment out;
    while (*iter) {
        DWORD wln = static_cast<DWORD>(wcslen(iter));
        DWORD ln = WideCharToMultiByte(CP_UTF8, 0, iter, wln,0,0,0,FALSE);
        buff.resize(ln);
        WideCharToMultiByte(CP_UTF8, 0, iter, wln, buff.data(), static_cast<DWORD>(buff.size()), 0 , FALSE);
        std::string_view s(buff.data(), buff.size());
        auto sp = s.find('=');
        if (s.npos != sp) {
            out.emplace(std::string(s.substr(0,sp)), std::string(s.substr(sp+1)));
        }
        iter = iter + wln + 1;
    }
    FreeEnvironmentStringsW(env);
    return out;

}

TwoPipesStreamData::TwoPipesStreamData(HANDLE in_fd, HANDLE out_fd, HANDLE process_mon, HANDLE hProcess)
    :AbstractHandleData(HandleType::two_pipes)
    ,_in_fd(in_fd)
    ,_out_fd(out_fd)
    ,_process_mon(process_mon)
    ,_hprocess(hProcess) {}

TwoPipesStreamData::~TwoPipesStreamData() {
    if (_in_fd != INVALID_HANDLE_VALUE) CloseHandle(_in_fd);
    if (_out_fd != INVALID_HANDLE_VALUE) CloseHandle(_out_fd);
    if (_process_mon != INVALID_HANDLE_VALUE) CloseHandle(_process_mon);
    if (_hprocess != NULL)  CloseHandle(_hprocess);
}

void TwoPipesStreamData::set_recv_buffer(char *buffer, std::size_t sz) {
    _recv_buffer = buffer;
    _recv_buffer_size = sz;
}
void TwoPipesStreamData::set_send_buffer(const char *buffer, std::size_t sz) {
    _send_buffer = buffer;
    _send_buffer_size = sz;
}

coro::prepared_coro TwoPipesStreamData::recv_async(std::chrono::system_clock::time_point tp, coro::awaitable<std::size_t>::result p) {
    if (_was_shutdown || _state_eof) return p(0);
    DWORD bytes;
    _recv_result = std::move(p);
    _recv_timeout = tp;
    if (!ReadFile(_in_fd, _recv_buffer, static_cast<DWORD>(_recv_buffer_size), &bytes, init_ovr(&_recv_ovr))) {
        DWORD e = GetLastError();
        if (e == ERROR_IO_PENDING) return {};
        else if (e == ERROR_BROKEN_PIPE) {_state_eof = true;return _recv_result(0);} 
        else return p.set_exception(std::make_exception_ptr(Win32Error(e, "ReadFile")));            
    } else {
        return on_complete(bytes, &_recv_ovr);
    }
}
coro::prepared_coro TwoPipesStreamData::send_async(std::chrono::system_clock::time_point tp, coro::awaitable<bool>::result p) {
    if (_was_shutdown || _send_closed) return p(false);
    DWORD bytes = 0;
    _send_result = std::move(p);
    _send_timeout = tp;
    if (!WriteFile(_out_fd, _send_buffer, static_cast<DWORD>(_send_buffer_size), &bytes, init_ovr(&_send_ovr))) {
        DWORD e = GetLastError();
        if (e == ERROR_IO_PENDING) return {};
        else if (e == ERROR_BROKEN_PIPE) return _send_result(false);
        else return p.set_exception(std::make_exception_ptr(Win32Error(e, "WriteFile")));
    } else {
        return on_complete(bytes, &_send_ovr);
    }
    
}

coro::prepared_coro TwoPipesStreamData::get_pid_status_async(std::chrono::system_clock::time_point tp, coro::awaitable<int>::result p) {
    DWORD bytes;
    _exit_result = std::move(p);
    _pidstat_timeout = tp;
    if (!ReadFile(_process_mon, _mon_buff, 1, &bytes, init_ovr(&_mon_ovr))) {
        DWORD e = GetLastError();
        if (e == ERROR_IO_PENDING) return {};
        else if (e == ERROR_BROKEN_PIPE) {
                WaitForSingleObject(_hprocess, INFINITE);
                DWORD ec;
                GetExitCodeProcess(_hprocess, &ec);
                return _exit_result(static_cast<int>(ec));
        } else return p.set_exception(std::make_exception_ptr(Win32Error(e, "WriteFile")));        
    } else {
        return on_complete(bytes, &_mon_ovr);   
    }   
}

coro::prepared_coro TwoPipesStreamData::on_complete(DWORD bytes, LPOVERLAPPED ovr) {
    if (ovr == &_recv_ovr) {
        _recv_timeout = _recv_timeout.max();
        return _recv_result(static_cast<std::size_t>(bytes));
    } else if (ovr == &_send_ovr) {
        _send_buffer += bytes;
        _send_buffer_size -= bytes;
        if (_send_buffer_size) {
            return send_async(_send_timeout, std::move(_send_result));
        } else {
            return _send_result(true);
        }
    }
    return {};
}
coro::prepared_coro TwoPipesStreamData::on_timeout(std::chrono::system_clock::time_point tp) {
    if (tp >= _recv_timeout) {
        CancelIoEx(_in_fd, &_recv_ovr);
        _recv_timeout = _recv_timeout.max();
    }
    if (tp >= _send_timeout) {
        CancelIoEx(_out_fd, &_send_ovr);
        _send_timeout = _send_timeout.max();
    }
    if (tp >= _pidstat_timeout) {
        CancelIoEx(_process_mon, &_mon_ovr);
        _pidstat_timeout = _pidstat_timeout.max();
    }
    return {};
}
coro::prepared_coro TwoPipesStreamData::on_shutdown() {
    if (!_was_shutdown) {
        _was_shutdown = true;
        CancelIoEx(_in_fd, &_recv_ovr);
        CancelIoEx(_out_fd, &_send_ovr);
    }
    return {};
}
coro::prepared_coro TwoPipesStreamData::on_error(DWORD error, LPOVERLAPPED ovr) {
    if (ovr == &_recv_ovr) {
        _recv_timeout = _recv_timeout.max();
        if (error == ERROR_OPERATION_ABORTED ) {
            if (_was_shutdown) return _recv_result.set_value(0);
            else return _recv_result.set_empty();
        }
        if (error == ERROR_BROKEN_PIPE) {
            _state_eof = true;
            return _recv_result.set_value(0);
        }
        else {
            return _recv_result.set_exception(std::make_exception_ptr(Win32Error(error,"Async read pipe")));
        }
    } else if (ovr == &_send_ovr) {
        if (error == ERROR_OPERATION_ABORTED ) {
            if (_was_shutdown) return _send_result.set_value(false);
            else return _send_result.set_empty();
        }
        if (error == ERROR_BROKEN_PIPE) {
            return _send_result.set_value(false);
        }
        else {
            return _recv_result.set_exception(std::make_exception_ptr(Win32Error(error,"Async write pipe")));
        }
    }  else if (ovr == &_mon_ovr) {
        if (error == ERROR_OPERATION_ABORTED) {
            return _exit_result.set_empty();
        } else if (error == ERROR_BROKEN_PIPE) {
            WaitForSingleObject(_hprocess, INFINITE);
            DWORD ec;
            GetExitCodeProcess(_hprocess, &ec);
            return _exit_result.set_value(static_cast<int>(ec));
        }
    }
    return {};
}

void TwoPipesStreamData::set_closing() {
    _closing = true;
}

void TwoPipesStreamData::send_close() {
    if (!_send_closed) {
        _send_closed = true;
        CloseHandle(_out_fd);
        _out_fd = INVALID_HANDLE_VALUE;
    }
}

StreamState TwoPipesStreamData::get_state() const {
    if (_state_eof) return StreamState::closed;
    if (_send_closed) return StreamState::closing;
    return StreamState::active;
}

bool TwoPipesStreamData::safe_to_close() const {
    return _closing && !_send_result && !_recv_result && !_exit_result;
}

std::chrono::system_clock::time_point TwoPipesStreamData::get_timeout() const {
    return std::min(std::min(_pidstat_timeout, _recv_timeout), _send_timeout);
}

bool TwoPipesStreamData::terminate_process() {
    if (_hprocess) {
        TerminateProcess(_hprocess, static_cast<UINT>(-9));
        return true;
    }
    return false;
}

BOOL CreatePipeEx(
    OUT LPHANDLE lpReadPipe,
    OUT LPHANDLE lpWritePipe,
    IN LPSECURITY_ATTRIBUTES lpPipeAttributes,
    IN DWORD nSize
) {

  static volatile long PipeSerialNumber;

  HANDLE ReadPipeHandle, WritePipeHandle;
  DWORD dwError;
  wchar_t PipeNameBuffer[ MAX_PATH ];

  if (nSize == 0) {
    nSize = 65536;
  }

  wsprintfW( PipeNameBuffer,
           L"\\\\.\\Pipe\\AnonPipe.%08x.%08x",
           GetCurrentProcessId(),
           InterlockedIncrement(&PipeSerialNumber)
         );

  ReadPipeHandle = CreateNamedPipeW(
                       PipeNameBuffer,
                       PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                       PIPE_TYPE_BYTE | PIPE_WAIT,
                       1,             // Number of pipes
                       nSize,         // Out buffer size
                       nSize,         // In buffer size
                       NMPWAIT_USE_DEFAULT_WAIT,    // Timeout in ms
                       lpPipeAttributes
                       );

  if (! ReadPipeHandle || INVALID_HANDLE_VALUE == ReadPipeHandle ) {
    return FALSE;
  }

  WaitNamedPipeW(PipeNameBuffer, INFINITE);

  WritePipeHandle = CreateFileW(
                      PipeNameBuffer,
                      GENERIC_WRITE,
                      0,                         // No sharing
                      lpPipeAttributes,
                      OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                      NULL                       // Template file
                    );

  if (INVALID_HANDLE_VALUE == WritePipeHandle) {
    dwError = GetLastError();
    CloseHandle( ReadPipeHandle );
    SetLastError(dwError);
    return FALSE;
  }

  *lpReadPipe = ReadPipeHandle;
  *lpWritePipe = WritePipeHandle;
  return( TRUE );
}


ContextImpl::Handle ContextImpl::connect_process(const std::filesystem::path &fpath, std::span<const std::string_view> argv,  const Environment & envp) {

    HANDLE hStdinRead = NULL, hStdinWrite = NULL;
    HANDLE hStdoutRead = NULL, hStdoutWrite = NULL;
    HANDLE hMonRead = NULL, hMonWrite = NULL;

    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

    try {

        if (!CreatePipeEx(&hStdinRead, &hStdinWrite, &sa, 0)) throw Win32Error("CreatePipe stdin");
        if (!CreatePipeEx(&hStdoutRead, &hStdoutWrite, &sa, 0)) throw Win32Error("CreatePipe stdout");
        if (!CreatePipeEx(&hMonRead, &hMonWrite, &sa, 0)) throw Win32Error("CreatePipe monitor handle");
        SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0); // parent write end not inherited
        SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0); // parent read end not inherited
        SetHandleInformation(hMonRead, HANDLE_FLAG_INHERIT, 0); // parent read end not inherited

        std::vector<char> args;
        for (const auto &z: argv) {
            args.push_back(' ');
            args.push_back('"');
            for (char c: z) {
                if (c == '"') {
                    args.push_back('\\');
                }
                args.push_back(c);
            }
            args.push_back('"');
        }
        std::vector<wchar_t> cmdline;
        cmdline.push_back(L'"');
        auto wpath = fpath.wstring();
        cmdline.insert(cmdline.end(), wpath.begin(), wpath.end());
        cmdline.push_back(L'"');
        auto sz = MultiByteToWideChar(CP_UTF8, 0, args.data(), static_cast<int>(args.size()),0,0);;
        auto nofs =cmdline.size();
        cmdline.resize(nofs+sz+1);
        MultiByteToWideChar(CP_UTF8, 0, args.data(), static_cast<int>(args.size()),cmdline.data()+nofs,sz);
        cmdline.back() = 0;

        std::vector<wchar_t> env_block;
        if (!envp.empty()) {
            for (const auto &[key, value] : envp) {
                auto ksz = MultiByteToWideChar(CP_UTF8, 0,key.data(), static_cast<int>(key.size()), 0,0);
                auto vsz =  MultiByteToWideChar(CP_UTF8, 0,value.data(), static_cast<int>(value.size()), 0,0);
                auto kofs = env_block.size();
                env_block.resize(kofs + ksz+vsz+2);
                MultiByteToWideChar(CP_UTF8, 0, key.data(), static_cast<int>(key.size()), env_block.data()+kofs, ksz);
                MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), env_block.data()+kofs+ksz+1, vsz);
                env_block[kofs+ksz] = '=';
                env_block[kofs+ksz+vsz+1] = 0;
            }
            env_block.push_back(0);
        }

        // 4. Spuštění procesu
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};

        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = hStdinRead;
        si.hStdOutput = hStdoutWrite;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE); // nebo můžeš vytvořit vlastní pipe pro STDERR


        BOOL result = CreateProcessW(
            NULL,
            cmdline.data(),
            NULL, NULL,
            TRUE, // dědění handlerů
            CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,
            env_block.empty() ?NULL:(LPVOID)env_block.data(),
            NULL, // current directory
            &si,
            &pi
        );

        if (!result) {
            throw Win32Error("CreateProcess failed: "+fpath.string());
        }


        CloseHandle(hStdinRead);
        CloseHandle(hStdoutWrite);
        CloseHandle(hMonWrite);
        CloseHandle(pi.hThread);

        std::lock_guard _(_mx);
        Handle h = _handleMap.emplace(PHandleData(new TwoPipesStreamData(hStdoutRead, hStdinWrite, hMonRead, pi.hProcess)));
        _iocp.add(hStdoutRead, h);
        _iocp.add(hStdinWrite, h);
        _iocp.add(hMonRead, h);
        return h;

    } catch (...) {
        if (hStdinRead) CloseHandle(hStdinRead);
        if (hStdoutRead) CloseHandle(hStdoutRead);
        if (hStdinWrite) CloseHandle(hStdinWrite);
        if (hStdoutWrite) CloseHandle(hStdoutWrite);
        if (hMonRead) CloseHandle(hMonRead);
        if (hMonWrite) CloseHandle(hMonWrite);
        throw;
    }
}

enum class StdType {
    std_in,
    std_out
};
template<StdType s>
static HANDLE make_overlapped_handle(HANDLE h) {
    HANDLE hR = INVALID_HANDLE_VALUE, hW = INVALID_HANDLE_VALUE;
    if (!CreatePipeEx(&hR, &hW, NULL, 0)) {
        throw Win32Error("CreatePipeEx (make_overlapped_handle)");
    }
    HANDLE read_end;
    HANDLE write_end;
    HANDLE close_end;
    HANDLE return_end;
    if constexpr(s == StdType::std_in) {
        read_end = h;
        write_end = hW;
        close_end = hW;
        return_end = hR;
    } else {
        read_end = hR;
        write_end = h;
        close_end = hR;
        return_end = hW;
    }


    std::thread thr([=]{
        while (true) {
            char buff[65536];
            DWORD rd, wr;
            if (!ReadFile(read_end, buff, sizeof(buff), &rd, NULL) || rd == 0) {
                CloseHandle(close_end);
                break;
            }
            char *c = buff;
            while (rd > 0) {
                if (!WriteFile(write_end, buff, rd, &wr, NULL)) {
                    CloseHandle(close_end);
                    break;
                }
                c+=wr;
                rd-=wr;
            }
        }
        CloseHandle(h); //close handle received by argument
    });
    thr.detach();
    return return_end;
}

ContextImpl::Handle ContextImpl::connect_stdinout() {

    HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    SetStdHandle(STD_INPUT_HANDLE, INVALID_HANDLE_VALUE);
    SetStdHandle(STD_OUTPUT_HANDLE, INVALID_HANDLE_VALUE);
    std::lock_guard _(_mx);
    auto ptr = new TwoPipesStreamData(hStdIn, hStdOut, INVALID_HANDLE_VALUE, NULL);
    Handle h = _handleMap.emplace(PHandleData(ptr));
    try {
        _iocp.add(hStdIn, h);
    } catch (const Win32Error &e) {
        if (e.code().value() == ERROR_INVALID_PARAMETER) {
            hStdIn = make_overlapped_handle<StdType::std_in>(hStdIn);
            _iocp.add(hStdIn, h);
            ptr->replace_in(hStdIn);
        } else {
            throw;
        }
    }
    try {
        _iocp.add(hStdOut, h);
    } catch (const Win32Error &e) {
        if (e.code().value() == ERROR_INVALID_PARAMETER) {
            hStdOut = make_overlapped_handle<StdType::std_out>(hStdOut);
            _iocp.add(hStdOut, h);
            ptr->replace_out(hStdOut);
        } else {
            throw;
        }
    }
    return h;
}
bool ContextImpl::terminate_process(Handle h) {
    std::lock_guard _(_mx);
    auto iter = _handleMap.find(h);
    if (iter == _handleMap.end()) return false;
    return iter->_value->visit([&](auto &p){
        using T = std::decay_t<decltype(p)>;
        if constexpr(std::is_same_v<T, TwoPipesStreamData>) {
            return p.terminate_process();
        } else {
            return false;
        }
    });
}
coro::awaitable<int> ContextImpl::get_process_exit_status(Handle h, std::chrono::system_clock::time_point tp) {
    return [this, h, tp](coro::awaitable<int>::result p) -> coro::prepared_coro{
        std::lock_guard _(_mx);
        if (!p) return p(false);
        auto iter = _handleMap.find(h);
        if (iter == _handleMap.end()) return p(false);
        return iter->_value->visit([&](auto &d) -> coro::prepared_coro {
            using T = std::decay_t<decltype(d)>;
            if constexpr(std::is_same_v<T, TwoPipesStreamData>) {
                return d.get_pid_status_async(tp, std::move(p));
            } else {
                return p(false);
            }
        });
    };
}

class BreakHandler {
public:
    static BreakHandler &getInstance() {
        static BreakHandler inst;
        return inst;
    }

    static BOOL WINAPI HandleRoutine(DWORD t) {
        return getInstance().handle_event(t);
    }

    BreakHandler() {
        SetConsoleCtrlHandler(&HandleRoutine, TRUE);
    }

    std::vector<coro::awaitable<ExitSignalType>::result> _awts;
    std::mutex _mx;

    BOOL handle_event(DWORD t) {
        ExitSignalType bt;
        switch (t) {
            case CTRL_C_EVENT : bt = ExitSignalType::control_c;break;
            case CTRL_BREAK_EVENT : bt = ExitSignalType::quit;break;
            case CTRL_CLOSE_EVENT: bt = ExitSignalType::terminal_close;break;
            default: bt = ExitSignalType::terminate;break;
        }
        std::vector<coro::prepared_coro> lst;
        lst.reserve(_awts.size());

        DWORD tk = GetTickCount();
        DWORD e = tk + 5000;    //up to 5 sec
        while (tk < e)  {   //cycle to catch any handler registered after CTRL+C
            {
                std::lock_guard _(_mx);
                std::transform(_awts.begin(), _awts.end(), std::back_inserter(lst),[&](auto &p){
                    return p(bt);
                });
            }
            lst.clear();
            Sleep(100);
            tk = GetTickCount();
        }
        return FALSE;//ExitProcess will be called there
    }

    void add(coro::awaitable<ExitSignalType>::result r) {
        std::lock_guard _(_mx);
        _awts.push_back(std::move(r));
    }

};

coro::awaitable<ExitSignalType> ContextImpl::wait_for_exit_signal() {
    return [](coro::awaitable<ExitSignalType>::result r) {
        BreakHandler::getInstance().add(std::move(r));
    };
}


}
