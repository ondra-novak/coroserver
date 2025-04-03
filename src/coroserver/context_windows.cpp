#include "win_mswsex.h"
#include "context_windows.hpp"
#include "context_inc.hpp"
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
    if (tp <= _tp) {
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
        }
    }
    return {};  //IOCP post is on way
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
    if (_tp <= tp) {
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
        }
        if (err == WSA_IO_PENDING) return {};
        return _recv_result.set_exception(std::make_exception_ptr(Win32Error(err, "WSARecv")));
    }
    //return on_complete(bytes, &_recv_ovr);
    return {}; //IOCP post is still on way
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
    //    return on_complete(bytes, &_send_ovr);
    return {}; //IOCP post is still on way
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
        if (prepared.empty()) {
            lk.unlock();
            auto ev =  _iocp.wait(_awaiting_tp);
            if (ev.error != ERROR_TIMEOUT) {
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
                pc = t.sleep_until(tp, std::move(p));
                update_timeout(t);
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
                p =srv.do_accept_async(timeout, std::move(r));
                if (!p) update_timeout(srv);
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
                        pc =  strm.recv_async(timeout, std::move(p));
                        if (!pc) update_timeout(strm);
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
                        pc =  strm.send_async(timeout, std::move(p));
                        if (!pc) update_timeout(strm);
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



}