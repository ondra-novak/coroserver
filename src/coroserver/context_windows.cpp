#include "win_mswsex.h"
#include "context_windows.hpp"
#include "context_inc.hpp"

#pragma comment(lib, "Ws2_32.lib")

static MsWSock mswsock;

namespace coroserver {

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
    :SocketHandleData(s),_af(af),_ctx(ctx) {}

coro::prepared_coro ServerHandleData::do_accept_async(
            std::chrono::system_clock::time_point tp, 
            coro::awaitable<Context::Handle>::result p) {    

    if (_was_shutdown) return p.set_value(0);

    ZeroMemory(&_ovr, sizeof(OVERLAPPED));
    SOCKET newSocket = socket(_af, SOCK_STREAM, IPPROTO_TCP);  //create socket
    if (newSocket == INVALID_SOCKET) {
        return p.set_exception(std::make_exception_ptr(Win32Error("socket")));
    }
    _prepared_socket = newSocket;
    DWORD rd = 0;
    constexpr auto bfs = sizeof(_accept_buffer)/2;
    _p = std::move(p);
    _tp = tp;
    BOOL res = mswsock.AcceptEx(_socket, _prepared_socket, 
                                _accept_buffer, 0, bfs, bfs, &rd, &_ovr);
    if (res) {
        return on_complete(0, &_ovr);
    } else {
        auto err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            return p.set_exception(std::make_exception_ptr(Win32Error(err, "AcceptEx")));
        }
        return {};
    }
}

coro::prepared_coro ServerHandleData::on_complete(DWORD, LPOVERLAPPED) {    
    if (_prepared_socket == INVALID_SOCKET) return {};
    setsockopt(_prepared_socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<char *>(&_socket), sizeof(SOCKET));
    sockaddr_storage *local, *remote;
    int local_sz = sizeof(local_sz), remote_sz = sizeof(remote_sz);
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
        CancelIoEx(_handle, &_ovr);
    }
    return {};
}

coro::prepared_coro ServerHandleData::on_shutdown() {
    if (!_was_shutdown) {
        _was_shutdown = true;
        if (_p) {
            CancelIoEx(_handle, &_ovr);
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

template<StreamType type>
StreamHandleData<type>::StreamHandleData(H h):SocketHandleData(h) {}

template<StreamType type>
void StreamHandleData<type>::set_recv_buffer(char *buffer, std::size_t sz) {
    _recv_buffer = buffer;
    _recv_buffer_size = sz;
}
template<StreamType type>
void StreamHandleData<type>::set_send_buffer(const char *buffer, std::size_t sz) {
    _send_buffer = buffer;
    _send_buffer_size = sz;
}


template<StreamType type>
coro::prepared_coro StreamHandleData<type>::recv_async(std::chrono::system_clock::time_point tp, coro::awaitable<std::size_t>::result p) {
    _recv_timeout = tp;
    _recv_result = std::move(p);
    update_timeout();
    DWORD bytes = 0;
    if constexpr(type == StreamType::socket) {
        WSABUF buff = {static_cast<ULONG>(_recv_buffer_size), _recv_buffer};
        if (!WSARecv(_socket,&buff,1,&bytes, 0, &_recv_ovr, NULL)) {
            DWORD err = WSAGetLastError();
            if (err == WSAECONNRESET || err == WSAECONNABORTED) {
                _state_eof = true;
                return _recv_result.set_value(0);
            }
            if (err == WSA_IO_PENDING) return {};
            return _recv_result.set_exception(std::make_exception_ptr(Win32Error(err, "WSARecv")));
        }
    } else {
        if (!ReadFile(_handle, _recv_buffer, static_cast<DWORD>(_recv_buffer_size), &bytes, &_recv_ovr)) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) return {};
            if (err == ERROR_BROKEN_PIPE) {
                _state_eof = true;
                return _recv_result.set_value(0);
            }
            return _recv_result.set_exception(std::make_exception_ptr(Win32Error(err, "ReadFile")));
        }
    }
    return on_complete(bytes, &_recv_ovr);
}

template<StreamType type>
coro::prepared_coro StreamHandleData<type>::send_async(std::chrono::system_clock::time_point tp, coro::awaitable<bool>::result p) {
    _send_timeout = tp;
    _send_result = std::move(p);
    update_timeout();
    if (_opening) return {};
    DWORD bytes = 0;
    if constexpr(type == StreamType::socket) {
        WSABUF buff = {static_cast<ULONG>(_send_buffer_size), const_cast<char *>(_send_buffer)};
        if (!WSASend(_socket, &buff, 1, &bytes, 0, &_send_ovr, NULL)) {
            DWORD err = WSAGetLastError();
            if (err == WSA_IO_PENDING) return {};
            if (err == WSAECONNRESET || err == WSAECONNABORTED) return _send_result.set_value(false);
            return _send_result.set_exception(std::make_exception_ptr(Win32Error(err, "WSASend")));
        }
    } else {
        if (!WriteFile(_handle,&_send_buffer, static_cast<DWORD>(_send_buffer_size), &bytes, &_send_ovr)) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) return {};
            if (err == ERROR_BROKEN_PIPE) return _recv_result.set_value(false);
            return _send_result.set_exception(std::make_exception_ptr(Win32Error(err, "WriteFile")));            
        }
    }
    return on_complete(bytes, &_send_ovr);
}

template<StreamType type>
coro::prepared_coro StreamHandleData<type>::on_complete(DWORD bytes, LPOVERLAPPED ovr) {
    if (ovr == &_send_ovr) {
        _opening = false;
        _send_buffer+=bytes;
        _send_buffer_size-= bytes;
        if (_send_buffer_size) {
            return send_async(_send_timeout, std::move(_send_result));
        }
        _send_timeout = _send_timeout.max();
        update_timeout();
        return _send_result(true);
    } else if (ovr == &_recv_ovr) {
        _send_timeout = _send_timeout.max();
        update_timeout();
        if (bytes == 0) _state_eof = 0;
        return _recv_result(bytes);
    }
    return {};
}

template<StreamType type>
coro::prepared_coro StreamHandleData<type>::on_timeout(std::chrono::system_clock::time_point tp) {
    if (tp >= _send_timeout) {
        CancelIoEx(_handle, &_send_ovr);
        _send_timeout = _send_timeout.max();        
    }
    if (tp >= _recv_timeout) {
        CancelIoEx(_handle, &_recv_ovr);
        _recv_timeout = _recv_timeout.max();
    }
    update_timeout();
    return {};
}

template<StreamType type>
coro::prepared_coro StreamHandleData<type>::on_shutdown() {
    _was_shutdown = true;
    CancelIoEx(_handle, &_send_ovr);
    CancelIoEx(_handle, &_recv_ovr);
    _send_timeout = _send_timeout.max();        
    _recv_timeout = _recv_timeout.max();
    update_timeout();
    return {};
}

template<StreamType type>
coro::prepared_coro StreamHandleData<type>::on_error(DWORD error, LPOVERLAPPED ovr) {
    if (ovr == &_send_ovr) {
        _send_timeout = _send_timeout.max();
        update_timeout();
        if (_opening) {
            _connect_error = error;            
            return _send_result.set_exception(std::make_exception_ptr(Win32Error(error, "Connect Error")));
        }
        if constexpr(type == StreamType::socket) {
            if (error == WSA_OPERATION_ABORTED || error == WSAECONNABORTED || error == WSAECONNRESET) {
                return _send_result.set_value(false);
            }            
        } else {
            if (error == ERROR_OPERATION_ABORTED || error == ERROR_BROKEN_PIPE) {
                return _send_result.set_value(false);
            }
        }
        return _send_result.set_exception(std::make_exception_ptr(Win32Error(error, "Async send")));
    } else if (ovr == &_recv_ovr) {
        _recv_timeout = _recv_timeout.max();
        update_timeout();
        if constexpr(type == StreamType::socket) {
            if (error == WSA_OPERATION_ABORTED) {
                if (_was_shutdown) return _recv_result.set_value(0);
                else return _recv_result.set_empty();
            }
            if (error == WSAECONNABORTED || error == WSAECONNRESET) {
                return _recv_result.set_value(0);
            }
        } else {
            if (error == ERROR_OPERATION_ABORTED) {
                if (_was_shutdown) return _recv_result.set_value(0);
                else return _recv_result.set_empty();
            }
            if (error == ERROR_BROKEN_PIPE) {
                return _recv_result.set_value(0);
            }
        }
        return _recv_result.set_exception(std::make_exception_ptr(Win32Error(error, "Async recv")));
    }
    return {};

}

template<StreamType type>
StreamState StreamHandleData<type>::get_state() const {
    if (_opening) return StreamState::opening;
    if (_state_eof) return StreamState::closed;
    if (_send_closed) return StreamState::closing;
    return StreamState::active;
}

template<StreamType type>
void StreamHandleData<type>::send_close() {
    if constexpr(type == StreamType::socket) {
        if (!_send_closed) {
            _send_closed = true;
            ::shutdown(_socket, SD_SEND);
        }
    } else {
        if (!_send_closed) {
            CloseHandle(_handle);
            _handle = INVALID_HANDLE_VALUE;
            _send_closed = true;
        }
    }
}

template<StreamType type>
void StreamHandleData<type>::update_timeout() {
    _tp = std::min(_send_timeout, _recv_timeout);
}

template<StreamType type>
bool StreamHandleData<type>::safe_to_close() const {
    return _closing && !_send_result && !_recv_result;
}
template<StreamType type>
StreamHandleData<type>::~StreamHandleData() {
    if constexpr(type == StreamType::socket) {
        closesocket(_socket);
    } else {
        if (_handle != INVALID_HANDLE_VALUE) CloseHandle(_handle);
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
                    auto r = hm._value->visit([&](auto &p) {
                        return p.on_timeout(now);
                    });
                    if (r) prepared.push_back(std::move(r));
                } else {
                    if (ctp < tp) tp = ctp;
                }
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
    return _handleMap.insert(std::make_unique<TimerHandleData>());
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
coro::awaitable<ContextImpl::Handle> ContextImpl::accept(Handle server, std::chrono::system_clock::time_point timeout) {

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
                        update_timeout(strm);
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
                        update_timeout(strm);
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



     
}