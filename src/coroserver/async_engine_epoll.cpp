#include "async_engine_epoll.h"

#include <csignal>
#include <linux/sockios.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
namespace coroserver {

AsyncEngineImpl::SocketReg &AsyncEngineImpl::SocketReg::from_handle(Handle h) {
    return *static_cast<SocketReg *>(h);
}

AsyncEngineImpl::AsyncEngineImpl():_epoll(epoll_create1(EPOLL_CLOEXEC)) {
    if (_epoll < 0) {
        int e = errno;
        throw std::system_error(e, std::system_category(), "epoll_create1");
    }
    epoll_event ev = {EPOLLIN, {.ptr = nullptr}};
    epoll_ctl(_epoll, EPOLL_CTL_ADD, _notify.getFD(), &ev);
}

AsyncEngineImpl::~AsyncEngineImpl() {

}

bool AsyncEngineImpl::is_blocked(SocketReg &reg) {
    return reg._blocked || _blocked_all;
}

AsyncEngineImpl::RetVal AsyncEngineImpl::recv(Handle h, void *buffer, std::size_t size, Timepoint timeout) {

    auto &reg = SocketReg::from_handle(h);
    int r = ::read(reg._socket, buffer, size);
    if (r >= 0) {
        return r;
    }
    int e = errno;
    if (e == EWOULDBLOCK) {

        return [&](auto promise) {
            std::lock_guard lk(_mx);
            if (is_blocked(reg)) return;
            auto &recv = reg._recv_state.emplace<RecvInfo>();
            recv._buffer = buffer;
            recv._buffer_size = size;
            recv._prom = std::move(promise);
            recv._tp = timeout;
            update_socket(reg);
        };

    } else if (e == EPIPE) {
        return 0;
    } else {
        return std::make_exception_ptr(std::system_error(e, std::system_category(), "recv"));
    }
}

int AsyncEngineImpl::recv_nb(Handle h, void *buffer, std::size_t size) {
    auto &reg = SocketReg::from_handle(h);
    int r = ::read(reg._socket, buffer, size);
    if (r >= 0) {
        return r;
    }
    int e = errno;
    if (e == EWOULDBLOCK || e == EPIPE) {
        return 0;
    }
    throw std::system_error(e, std::system_category(), "recv_nb");

}

AsyncEngineImpl::RetVal AsyncEngineImpl::send(Handle h,const void *buffer, std::size_t size, Timepoint timeout) {

    auto &reg = SocketReg::from_handle(h);
    int r = ::write(reg._socket, buffer, size);
    if (r >= 0) {
        return r;
    }
    int e = errno;
    if (e == EWOULDBLOCK) {

        return [&](auto promise) {
            std::lock_guard lk(_mx);
            if (is_blocked(reg)) return;
            auto &send = reg._send_state.emplace<SendInfo>();
            send._buffer = buffer;
            send._buffer_size = size;
            send._prom = std::move(promise);
            send._tp = timeout;
            update_socket(reg);
        };

    } else if (e == EPIPE) {
        return 0;
    } else {
        return std::make_exception_ptr(std::system_error(e, std::system_category(), "recv"));
    }
}

void AsyncEngineImpl::update_socket(SocketReg &reg) {
    int flags = 0;
    if (!std::holds_alternative<InfoEmpty>(reg._recv_state)) {
        flags |= EPOLLIN;
    }
    if (!std::holds_alternative<InfoEmpty>(reg._send_state)) {
        flags |= EPOLLOUT;
    }
    if (flags) {
        epoll_event ev = {flags | EPOLLONESHOT, {.ptr = &reg}};
        if (epoll_ctl(_epoll, EPOLL_CTL_MOD, reg._socket, &ev)<0) {
            int e = errno;
            if (e == ENOENT) {
                if (epoll_ctl(_epoll, EPOLL_CTL_ADD, reg._socket, &ev)<0) {
                    e = errno;
                    throw std::system_error(e, std::system_category(), "epoll_ctl add");
                }
            } else {
                throw std::system_error(e, std::system_category(), "epoll_ctl mod");
            }
        }
    }
    if (update_timeout(reg)) {
        _notify.add(1);
    }
}

AsyncEngineImpl::RetVal AsyncEngineImpl::wait_connect(Handle h,Timepoint timeout) {
    auto &reg = SocketReg::from_handle(h);
    if (std::holds_alternative<ConnectNamedPipeInfo>(reg._recv_state)
            || std::holds_alternative<ConnectNamedPipeInfo>(reg._send_state)) {
        return [&](auto promise) {
            ConnectNamedPipeInfo &connect =
                    std::holds_alternative<ConnectNamedPipeInfo>(reg._recv_state)?
                            std::get<ConnectNamedPipeInfo>(reg._recv_state):
                            std::get<ConnectNamedPipeInfo>(reg._send_state);
            connect._prom = std::move(promise);
            connect._tp = timeout;
        };
    } else {
        return [&](auto promise) {
            std::lock_guard lk(_mx);
            if (is_blocked(reg)) return;
            auto &connect = reg._send_state.emplace<ConnectInfo>();
            connect._prom = std::move(promise);
            connect._tp = timeout;
            update_socket(reg);
        };
    }

}

AsyncEngineImpl::RetVal AsyncEngineImpl::accept(Handle h, Handle &retHandle,
                    PeerName &retPeerName, Timepoint timeout) {
    int r;
    auto &reg = SocketReg::from_handle(h);
    retPeerName =  PeerName::capture_sockaddr([&](sockaddr *saddr, socklen_t slen){
        r = ::accept4(reg._socket, saddr, &slen, SOCK_CLOEXEC|SOCK_NONBLOCK);
        return r<0?0:slen;
    });
    if (r>=0) {
        retHandle = new SocketReg(r);
        return 1;
    }
    int e = errno;
    if (e == EWOULDBLOCK || e == EINPROGRESS) {
        return [&](auto promise) {
            std::lock_guard lk(_mx);
            if (is_blocked(reg)) return;
            auto &accept = reg._recv_state.emplace<AcceptInfo>();
            accept._prom = std::move(promise);
            accept._tp = timeout;
            accept._handle = &retHandle;
            accept._peerName = &retPeerName;
            update_socket(reg);

        };
    }
    return std::make_exception_ptr(std::system_error(e, std::system_category(), "Accept failed"));
}

void AsyncEngineImpl::send_eof(Handle h) {
    auto &reg = SocketReg::from_handle(h);
    if (reg._pipe) {
        ::close(reg._socket.release());
    } else {
        ::shutdown(reg._socket,SHUT_WR);
    }
}

AsyncEngineImpl::Handle AsyncEngineImpl::listen(const PeerName &ifc) {
    FileDescriptor sock = ifc.use_sockaddr([&](const sockaddr *addr, socklen_t slen){

        FileDescriptor fd = ::socket(addr->sa_family,
                SOCK_STREAM| SOCK_CLOEXEC|SOCK_NONBLOCK,
                (addr->sa_family == AF_INET || addr->sa_family == AF_INET6)?IPPROTO_TCP:0);
        if (!fd) {
            throw std::system_error(errno, std::system_category(), "::socket failed (listen)");
        }
        if (addr->sa_family == AF_INET6) {
           int on = 1;
           if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (const void *)&on, sizeof(on)) == -1) {
               throw std::system_error(errno, std::system_category(), "can't disable IPv4-mapped (setsockopt)");
           }
        }

        if (::bind(fd, addr, slen)) {
            throw std::system_error(errno, std::system_category(), "Unable to bind listening socket to " + ifc.to_string());
        }
        if (::listen(fd,SOMAXCONN)) {
            throw std::system_error(errno, std::system_category(), "Unable to listen socket");
        }
        return fd;
    });

    return new SocketReg(std::move(sock));
}

AsyncEngineImpl::Handle AsyncEngineImpl::connect(const PeerName &target) {
    FileDescriptor sock = target.use_sockaddr([&](const sockaddr *addr, socklen_t slen){

        FileDescriptor fd = ::socket(addr->sa_family,
                SOCK_STREAM| SOCK_CLOEXEC|SOCK_NONBLOCK,
                (addr->sa_family == AF_INET || addr->sa_family == AF_INET6)?IPPROTO_TCP:0);
        if (!fd) {
            throw std::system_error(errno, std::system_category(), "::socket failed (listen)");
        }
        if (::connect(fd, addr, slen)) {
            int e = errno;
            if (e != EAGAIN &&  e != EINPROGRESS && e != EWOULDBLOCK) {
                throw std::system_error(errno, std::system_category(), "Connect failed to " + target.to_string());
            }
        }
        return fd;
    });

    return new SocketReg(std::move(sock));
}

static void setnonblocking(int sock) {
    int opt;

    opt = fcntl(sock, F_GETFL);
    if (opt < 0) {
        printf("fcntl(F_GETFL) fail.");
    }
    opt |= O_NONBLOCK;
    if (fcntl(sock, F_SETFL, opt) < 0) {
        printf("fcntl(F_SETFL) fail.");
    }
}


AsyncEngineImpl::Handle AsyncEngineImpl::from_fd(int fd) {
    setnonblocking(fd);
    return new SocketReg(FileDescriptor(fd));
}

AsyncEngineImpl::Handle AsyncEngineImpl::connect_special(SpecialDevice dev) {
    switch (dev) {
        case SpecialDevice::std_input: return from_fd(0);
        case SpecialDevice::std_output: return from_fd(1);
        case SpecialDevice::std_error: return from_fd(2);
        default: throw std::invalid_argument("Invalid SpecialDevice");
    }
}

AsyncEngineImpl::Handle AsyncEngineImpl::open_file(OperationMode mode, std::string name, bool append) {

    int flags = 0;
    mode_t creat_mode = 0666;
    switch (mode) {
        case OperationMode::bidirectional:flags = O_RDWR; break;
        case OperationMode::read: flags = O_RDONLY;break;
        case OperationMode::write: flags = O_WRONLY;
            if (append) {
                flags |= O_CREAT | O_APPEND;
            } else {
                flags |= O_CREAT | O_TRUNC;
            }
            break;
        default: throw std::invalid_argument("Invalid mode");
    }

    FileDescriptor fd ( ::open(name.c_str(), flags, creat_mode));
    if (!fd) {
        int e = errno;
        throw std::system_error(e, std::system_category(), "Can't open file: " + name);
    }
    auto r = new SocketReg(std::move(fd));
    r->_pipe = true;
    return r;

}

AsyncEngineImpl::Handle AsyncEngineImpl::create_named_pipe(OperationMode mode, std::string name) {
    if (mode == OperationMode::bidirectional) {
        throw std::invalid_argument("OperationMode::bidirectional is not supported here");
    }
    if (name.empty()) throw std::invalid_argument("create_named_pipe: <name> is empty");
    if (name[0] != '/' && name[0] != '.') {
        auto uid = geteuid();
        std::ostringstream buff;
        if (uid) {
            buff << "/var/run/user/" << uid << "/" << name;
        } else {
            buff << "/var/run/" << name;
        }
        return create_named_pipe(mode, buff.str());
    }

    if (mkfifo(name.c_str(),0666) == -1) {
        int e = errno;
        if (e == EEXIST) {
            struct stat buff;
            if (stat(name.c_str(), &buff) == -1 || !S_ISFIFO(buff.st_mode))
                throw std::system_error(e, std::system_category(), "mkfifo");
        } else {
            throw std::system_error(e, std::system_category(), "mkfifo");
        }
    }

    auto h = open_file(mode, name, false);
    auto &reg = SocketReg::from_handle(h);
    if (mode == OperationMode::read) {
        reg._recv_state = ConnectNamedPipeInfo();
    } else if (mode == OperationMode::write) {
        reg._send_state = ConnectNamedPipeInfo();
    }
    return h;


}

void AsyncEngineImpl::block_all(bool block) {
    coro::promise<int> to_cancel;
    std::lock_guard _(_mx);
    if (block) {
        for (auto *reg: _tm_map) {
            std::visit([&](auto &s, auto &r){
                to_cancel += s._prom;
                to_cancel += r._prom;
            }, reg->_send_state, reg->_recv_state);
            reg->_recv_state.emplace<InfoEmpty>();
            reg->_send_state.emplace<InfoEmpty>();
        }
        _tm_map.clear();
    }
    _blocked_all = block;
}

bool AsyncEngineImpl::update_timeout(SocketReg &reg) {
    _tm_map.erase(&reg);
    return insert_timeout(reg);
}

bool AsyncEngineImpl::insert_timeout(SocketReg &reg) {
    if (std::holds_alternative<InfoEmpty>(reg._send_state)
        && std::holds_alternative<InfoEmpty>(reg._recv_state)) {
        return false;
    }

    reg._timeout = std::visit([&](const auto &x, const auto &y){
        return std::min(x._tp, y._tp);
    }, reg._recv_state, reg._send_state);


    bool updated = _next_wakeup > reg._timeout;
    if (updated) _next_wakeup == reg._timeout;

    _tm_map.insert(&reg).first == _tm_map.begin();
    return updated;
}

void AsyncEngineImpl::block(Handle h, bool blocked) {
    auto &reg = SocketReg::from_handle(h);
    coro::promise<int> to_cancel;
    std::lock_guard _(_mx);

    if (blocked) {
        std::visit([&](auto &x, auto &y){
            to_cancel += x._prom;
            to_cancel += y._prom;
        }, reg._recv_state, reg._send_state);
        reg._recv_state.emplace<InfoEmpty>();
        reg._send_state.emplace<InfoEmpty>();
    }

    reg._blocked = blocked;
}

void AsyncEngineImpl::close_handle(Handle h) {
    auto &reg = SocketReg::from_handle(h);
    epoll_ctl(_epoll, EPOLL_CTL_DEL, reg._socket, nullptr);
    {
        std::lock_guard _(_mx);
        _tm_map.erase(&reg);
    }
    _to_free.push_back(std::unique_ptr<SocketReg>(&reg));
}


AsyncEngineImpl::Notify AsyncEngineImpl::wait_for_next_event(Timepoint timeout) {
    int waittm;
    std::unique_lock lk(_mx);
    if (_ready.empty()) {
        int r;
        epoll_event ev;
        do {
            Timepoint wakeup_time = _tm_map.empty()?maxtp:(*_tm_map.begin())->_timeout;
            timeout = std::min(timeout, wakeup_time);
            _next_wakeup = timeout;
            lk.unlock();
            auto now = std::chrono::system_clock::now();
            if (timeout < now) {
                waittm = 0;
            } else if (timeout == maxtp) {
                waittm = -1;
            } else {
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout-now).count();
                ms = std::min(ms, static_cast<decltype(ms)>(std::numeric_limits<int>::max()));
                waittm = static_cast<int>(ms);
            }

            r = epoll_wait(_epoll, &ev, 1, waittm);
            if (r < 0) {
                int e = errno;
                if (e != EINTR) {
                    throw std::system_error(e, std::system_category(), "epoll_wait failed");
                }
                lk.lock();
            }
        } while (r < 0);
        lk.lock();
        if (r == 0) {
            auto now = std::chrono::system_clock::now();
            auto iter = _tm_map.begin();
            while (iter != _tm_map.end() && (*iter)->_timeout < now) {
                auto &reg = *(*iter);
                auto [tm1,tm2] = std::visit([&](auto &x, auto &y){
                    return std::pair(x._tp, y._tp);
                }, reg._send_state, reg._recv_state);
                if (tm1 < now) {
                    std::visit([&](auto &x){_ready.push(x._prom(-1));}, reg._send_state);
                    reg._send_state.emplace<InfoEmpty>();
                }
                if (tm2 < now) {
                    std::visit([&](auto &x){_ready.push(x._prom(-1));}, reg._recv_state);
                    reg._recv_state.emplace<InfoEmpty>();
                }
                iter = _tm_map.erase(iter);
                insert_timeout(reg);
            }
        } else {
            if (ev.data.ptr == nullptr) {
                _notify.fetch_and_reset();
            } else {
                auto &reg = *reinterpret_cast<SocketReg *>(ev.data.ptr);
                if (ev.events & (EPOLLIN| EPOLLERR)) {
                    if (std::holds_alternative<AcceptInfo>(reg._recv_state)) {
                        auto &a = std::get<AcceptInfo>(reg._recv_state);
                        *a._peerName = PeerName::capture_sockaddr([&](sockaddr *addr, socklen_t slen){
                            r = ::accept4(reg._socket,addr, &slen, SOCK_CLOEXEC|SOCK_NONBLOCK);
                            return r<0?0:slen;
                        });
                        if (r >= 0) {
                            a._tp = maxtp;
                            *a._handle = new SocketReg(r);
                            _ready.push(a._prom(1));
                            reg._recv_state.emplace<InfoEmpty>();
                        } else {
                            int e = errno;
                            if (e != EWOULDBLOCK) {
                                a._tp = maxtp;
                                _ready.push(
                                        a._prom.reject(std::system_error(e,std::system_category(),"accept"))
                                );
                                reg._recv_state.emplace<InfoEmpty>();
                            }
                        }
                    }
                    else if (std::holds_alternative<RecvInfo>(reg._recv_state)) {
                        auto &me = std::get<RecvInfo>(reg._recv_state);
                        int r = ::read(reg._socket, me._buffer, me._buffer_size);
                        if (r >= 0) {
                            _ready.push(me._prom(r));
                            reg._recv_state.emplace<InfoEmpty>();
                        } else {
                            int e = errno;
                            if (e != EWOULDBLOCK) {
                                if (e == ECONNRESET) {
                                    _ready.push(me._prom(0));
                                } else {
                                    _ready.push(
                                        me._prom.reject(std::system_error(e,std::system_category(),"recv"))
                                    );
                                }
                                reg._recv_state.emplace<InfoEmpty>();                            }
                        }
                    }
                    else if (std::holds_alternative<ConnectNamedPipeInfo>(reg._recv_state)) {
                        auto &me = std::get<ConnectNamedPipeInfo>(reg._recv_state);
                        _ready.push(me._prom(1));
                        reg._recv_state.emplace<InfoEmpty>();
                    }
                }
                if (ev.events & (EPOLLOUT| EPOLLERR)) {
                    if (std::holds_alternative<ConnectInfo>(reg._send_state)) {
                        auto &me = std::get<ConnectInfo>(reg._send_state);
                        int error_code;
                        socklen_t error_code_size = sizeof(error_code);
                        ::getsockopt(reg._socket, SOL_SOCKET, SO_ERROR, &error_code, &error_code_size);
                        if (!error_code) {
                            _ready.push(me._prom(1));
                        } else {
                            _ready.push(
                                me._prom.reject(std::system_error(error_code,std::system_category(),"connect"))
                            );
                        }
                        reg._send_state.emplace<InfoEmpty>();
                    }
                    else if (std::holds_alternative<SendInfo>(reg._send_state)) {
                        auto &me = std::get<SendInfo>(reg._send_state);
                        int r = ::write(reg._socket, me._buffer, me._buffer_size);
                        if (r >= 0) {
                            _ready.push(me._prom(r));
                            me._tp = maxtp;
                        } else {
                            int e = errno;
                            if (e == EPIPE) {
                                _ready.push(me._prom(0));
                                reg._send_state.emplace<InfoEmpty>();
                            } else if (e != EWOULDBLOCK) {
                                _ready.push(
                                    me._prom.reject(std::system_error(e,std::system_category(),"send"))
                                );
                                reg._send_state.emplace<InfoEmpty>();
                            }
                        }
                    }
                    else if (std::holds_alternative<ConnectNamedPipeInfo>(reg._send_state)) {
                        auto &me = std::get<ConnectNamedPipeInfo>(reg._send_state);
                        _ready.push(me._prom(1));
                        reg._send_state.emplace<InfoEmpty>();
                    }
                }
                update_socket(reg);
            }
        }
        if (_ready.empty()) return {};
    }
    auto n = std::move(_ready.front());
    _ready.pop();
    _to_free.clear();
    return n;

}

AsyncEngineImpl::Notify AsyncEngineImpl::wait_for_next_event() {
    return wait_for_next_event(maxtp);
}

void AsyncEngineImpl::cancel_wait_for_next_event() {
    _notify.add(1);
}

int AsyncEngineImpl::get_siocoutq(Handle h) {
    auto &reg = SocketReg::from_handle(h);
    int value = 0;
    if (!reg._pipe) ioctl(reg._socket, SIOCOUTQ, &value);
    return value;
}

PeerName AsyncEngineImpl::get_name(Handle h) {
    auto &reg = SocketReg::from_handle(h);
    return PeerName::capture_sockaddr([&](sockaddr *saddr, socklen_t slen){
        if (getsockname(reg._socket, saddr, &slen)<0) {
            throw std::system_error(errno, std::system_category(), "getsockname failed");
        }
        return slen;
    });
}

std::optional<EFDEventRegister> _signal_reg;

void AsyncEngineImpl::signal_handler(int) {
    if (_signal_reg.has_value()) {
        _signal_reg->add(1);
    }
}

void AsyncEngineImpl::install_intr_signal_impl() {
    _signal_reg.emplace();
    for (int i: std::initializer_list<int>{SIGTERM, SIGINT, SIGHUP, SIGQUIT}) {
        signal(i, signal_handler);
    }
}

AsyncEngineImpl::Handle AsyncEngineImpl::install_intr_signal() {
    if (!_signal_reg.has_value()) install_intr_signal_impl();
    return from_fd(dup(_signal_reg->getFD()));
}

}
