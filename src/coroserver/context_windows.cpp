#include "win_mswsex.h"
#include "context_windows.hpp"
//#include "context_inc.hpp"

#pragma comment(lib, "Ws2_32.lib")

static MsWSock mswsock;

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

ServerHandleData::ServerHandleData(SOCKET s, int af, ContextImpl *ctx)
    :SocketHandleData(s),_af(af),_ctx(ctx) {}

coro::prepared_coro ServerHandleData::do_accept_async(
            std::chrono::system_clock::time_point tp, 
            coro::awaitable<Context::Handle>::result p) {    

    if (_shutted_down) return p.set_value(0);

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
        return on_complete();
    } else {
        auto err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            return p.set_exception(std::make_exception_ptr(Win32Error(err, "AcceptEx")));
        }
        return {};
    }
}

coro::prepared_coro ServerHandleData::on_complete() {
    if (_prepared_socket == INVALID_SOCKET) return {};
    setsockopt(_prepared_socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<char *>(&_socket), sizeof(SOCKET));
    sockaddr_storage *local, *remote;
    int local_sz = sizeof(local_sz), remote_sz = sizeof(remote_sz);
    constexpr auto bfs = sizeof(_accept_buffer)/2;
    mswsock.GetAcceptExSockaddrs(_accept_buffer,0,bfs,bfs,
        reinterpret_cast<sockaddr **>(&local), &local_sz, reinterpret_cast<sockaddr **>(&remote), &remote_sz);

    auto h = _ctx->handleFromSocket(_prepared_socket);
    _prepared_socket = INVALID_SOCKET;
    _tp = _tp.max();
    return _p.set_value(h);
}

coro::prepared_coro ServerHandleData::on_error(DWORD err, LPOVERLAPPED ovr) {
    if (ovr == &_ovr) {
        _tp = _tp.max();
        if (err == ERROR_OPERATION_ABORTED) {
            if (_shutted_down) {
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
    if (!_shutted_down) {
        _shutted_down = true;
        if (_p) {
            CancelIoEx(_handle, &_ovr);
        }
    }
    return {};
}


     
}