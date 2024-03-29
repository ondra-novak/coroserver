#include "epoll.h"

#include <sys/epoll.h>
#include <vector>

namespace coroserver {

AsyncEPoll::AsyncEPoll():_epoll(epoll_create1(EPOLL_CLOEXEC)) {
    if (_epoll < 0) {
        int e = errno;
        throw std::system_error(e, std::system_category(), "epoll_create1");
    }
    epoll_event ev = {EPOLLONESHOT|EPOLLIN, {.fd = _notify.getFD()}};
    epoll_ctl(_epoll, EPOLL_CTL_ADD, _notify.getFD(), &ev);
}


void AsyncEPoll::register_socket(coro::promise<bool> prom, int fd, Operation op, std::chrono::system_clock::time_point timeout) {
    if (_stopped) return;
    WaitReg &reg = _regs[fd];
    if (reg._shutdown) return;
    int idx = static_cast<int>(op);
    reg._items[idx]._prom += prom;
    reg._items[idx]._timeout = timeout;
    update_socket(fd, reg);
}


void AsyncEPoll::do_serve(ICb &&scheduler, std::stop_token stoken) {
    std::vector<coro::promise<bool>::notify> output;
    std::stop_callback _cb(stoken, [&]{
        std::lock_guard _(_mx);
        _stopped = true;
        _notify.add(1);
    });

    bool cnt = true;

    auto now = std::chrono::system_clock::now();
    auto wkt = now;

    do {

        int timeout_ms;
        if (wkt == max_timeout) timeout_ms = -1;
        else if (wkt <= now) timeout_ms = 0;
        else {
            auto span = std::chrono::duration_cast<std::chrono::milliseconds>(wkt - now).count();
            if (span > std::numeric_limits<int>::max()) {
                timeout_ms = std::numeric_limits<int>::max();
            } else {
                timeout_ms = static_cast<int>(span);
            }
        }

        epoll_event events[16];
        int r = epoll_wait(_epoll, events, 16, timeout_ms);
        now = std::chrono::system_clock::now();
        if (r<0) {
            int e = errno;
            if (e == EINTR) continue;
            throw std::system_error(e, std::system_category(), "epoll_wait");
        }
        {
            std::lock_guard _(_mx);

            if (r == 0) {
                wakeup_time = max_timeout;
                for (auto &[fd, reg]: _regs) {
                    for(auto &itm: reg._items) {
                        if (itm._timeout < now) {
                            output.push_back(itm._prom(false));
                            itm._timeout = max_timeout;
                        }
                    }
                    update_timeout(reg);
                }
                while (!_scheduled.empty() && _scheduled.front()._tp < now) {
                     output.push_back(_scheduled.front()._prom(true));
                     std::pop_heap(_scheduled.begin(), _scheduled.end());
                     _scheduled.pop_back();
                }
                if (!_scheduled.empty() && _scheduled.front()._tp < wakeup_time) {
                    wakeup_time = _scheduled.front()._tp;
                }
            } else {
                for (int i = 0; i < r;  ++i) {
                    const auto &ev = events[i];
                    if (ev.data.fd == _notify.getFD()) {
                        epoll_event new_ev = {EPOLLIN| EPOLLONESHOT, {.fd = ev.data.fd}};
                        _notify.fetch_and_reset();
                        epoll_ctl(_epoll,EPOLL_CTL_MOD, ev.data.fd,&new_ev);
                    } else {
                        auto iter = _regs.find(ev.data.fd);
                        if (iter != _regs.end()) {
                            if (ev.events & (EPOLLIN|EPOLLERR)) {
                                output.push_back(iter->second._items[0]._prom(true));
                                iter->second._items[1]._timeout = max_timeout;
                            }
                            if (ev.events & (EPOLLOUT|EPOLLERR)) {
                                output.push_back(iter->second._items[1]._prom(true));
                                iter->second._items[2]._timeout = max_timeout;
                            }
                        }
                    }

                }
            }
            wkt = wakeup_time;
            cnt = !_stopped;
        }
        for (auto &x: output) {
            scheduler(std::move(x));
        }
        output.clear();
    } while (cnt);
}


void AsyncEPoll::update_socket(int fd, const WaitReg &reg) {
    int flags = 0;
    if (reg._items[0]._prom) flags |= EPOLLIN;
    if (reg._items[1]._prom) flags |= EPOLLOUT;
    if (flags) {
        epoll_event ev = {flags | EPOLLONESHOT, {.fd = fd}};
        if (epoll_ctl(_epoll, EPOLL_CTL_MOD, fd, &ev)<0) {
            int e = errno;
            if (e == ENOENT) {
                if (epoll_ctl(_epoll, EPOLL_CTL_ADD, fd, &ev)<0) {
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

void AsyncEPoll::shutdown(int fd) {
    do_shutdown(fd);
}

void AsyncEPoll::unreg(int fd) {
    do_unreg(fd);

}

bool AsyncEPoll::update_timeout(const WaitReg &reg) {
    bool modified = false;
    for (const auto &itm: reg._items) {
        if (itm._timeout < wakeup_time) {
            wakeup_time = itm._timeout;
            modified = true;
        }
    }
    return modified;
}


AsyncEPoll::WaitReg AsyncEPoll::do_shutdown(int fd) {
    std::lock_guard _(_mx);
    WaitReg &reg = _regs[fd];
    reg._shutdown = true;
    return std::move(reg);
}

AsyncEPoll::WaitReg AsyncEPoll::do_unreg(int fd) {
    std::lock_guard _(_mx);
    WaitReg reg = std::move(_regs[fd]);
    _regs.erase(fd);
    return reg;
}

coro::future<bool> AsyncEPoll::wait(std::chrono::system_clock::time_point timeout, const void *ident) {
    return [&](auto promise) {
        std::lock_guard _(_mx);
        if (std::find(_blocked.begin(), _blocked.end(), ident) != _blocked.end()) {
            promise(false);
        } else {
            _scheduled.push_back({timeout, ident, std::move(promise)});
            std::push_heap(_scheduled.begin(), _scheduled.end());
            if (timeout < wakeup_time) {
                _notify.add(1);
                wakeup_time = timeout;
            }
        }
    };
}

std::unique_ptr<AsyncEPoll, AsyncEPoll::__blocker> AsyncEPoll::cancel(const void *ident) {
    coro::promise<bool>::notify ntf;
    std::lock_guard _(_mx);
    _blocked.push_back(ident);
    auto iter = std::find_if(_scheduled.begin(), _scheduled.end(), [&](const ScheduledItem &itm){
        return itm.ident == ident;
    });
    if (iter != _scheduled.end()) {
        ntf = iter->_prom(false);
        if (iter == _scheduled.begin()) {
            std::pop_heap(_scheduled.begin(), _scheduled.end());
            _scheduled.pop_back();
        } else {
            if (&(*iter) != &_scheduled.back()) {
                std::swap(*iter, _scheduled.back());
                _scheduled.pop_back();
                std::make_heap(_scheduled.begin(), _scheduled.end());
            } else {
                _scheduled.pop_back();
            }
        }
    }
    return {this, {ident}};
}


void AsyncEPoll::unblock(const void *ident) {
    std::lock_guard _(_mx);
    auto r = std::remove(_blocked.begin(), _blocked.end(), ident);
    _blocked.erase(r, _blocked.end());
}

coro::future<bool> AsyncEPoll::wait(int fd, Operation op, std::chrono::system_clock::time_point timeout) {
    return [&](auto promise) {
        register_socket(std::move(promise), fd, op, timeout);
    };
}

}
