/*
 * dispatcher_epoll.cpp
 *
 *  Created on: 27. 1. 2021
 *      Author: ondra
 */

#ifndef _WIN32

#include "poller_epoll.h"

#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <sys/epoll.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <iostream>
namespace coroserver {


static int init_epoll_object() {
    int e = epoll_create1(EPOLL_CLOEXEC);
    if (e < 0) {
        int er = errno;
        throw std::system_error(er,std::generic_category(), "epoll_create1");
    }
    return e;
}


Poller_epoll::Poller_epoll()
:_epoll_fd(init_epoll_object())
,first_timeout(std::chrono::system_clock::time_point::min())
{
    epoll_event ev ={};
    ev.events = EPOLLIN;
    ev.data.fd = _ntf_event.getFD();
    int r = epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, _ntf_event.getFD(), &ev);
    if (r < 0) {
        int e = errno;
        throw std::system_error(e,std::generic_category(), "event_notify");
    }

}

coro::future<void> Poller_epoll::start(coro::scheduler &scheduler) {
    try {
        std::lock_guard _(_mx);
        if (_running) throw std::logic_error("Already running");
        _running = true;
        _request_stop = false;
        _current_scheduler = &scheduler;
        coro::target_simple_activation(_ublock_trg, [&](coro::scheduler *sch){
            notify();
            sch->register_unblock(_ublock_trg);
        });
        scheduler.register_unblock(_ublock_trg);

        return [&](auto prom) {
            _end_promise = std::move(prom);
            scheduler.schedule([&]{worker();});
        };

    } catch (...) {
        return coro::future<void>(std::current_exception());
    }
}

void Poller_epoll::stop() {
     FDMap tmp;
     epoll_event ev ={};
     {
         std::lock_guard _(_mx);
         if (!_running || _request_stop) return;
         for (auto &x: fd_map) {
             epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, x.first, &ev);
         }
         std::swap(tmp ,fd_map);
         _request_stop = true;
         _current_scheduler->unregister_unblock(_ublock_trg);
         notify();
     }
}



Poller_epoll::~Poller_epoll() {

    Poller_epoll::stop();
    std::unique_lock lk(_mx);
    if (_running) {
        _cond.wait(lk,[&]{return !_running;});
    }
}

WaitResult Poller_epoll::io_wait(SocketHandle s,
                           AsyncOperation op,
                           std::chrono::system_clock::time_point timeout)  {
    return [&](auto p) {
        std::lock_guard _(_mx);
        if (!_running || _request_stop || mclosing_map.find(s) != mclosing_map.end()) {
            return;
        }
        auto opindex = static_cast<int>(op);
        Reg reg{timeout, std::move(p)};
        bool first;

        auto iter = fd_map.find(s);
        if (iter == fd_map.end()) {
            RegList lst;
            lst[opindex] = std::move(reg);
            iter = fd_map.emplace(s, std::move(lst)).first;
            first = true;
        } else {
            first = false;
            iter->second[opindex] = std::move(reg);
        }
        rearm_fd(first, iter);
    };
}


void Poller_epoll::shutdown(SocketHandle s) {
    RegList tmp;
     epoll_event ev ={};
    {
        std::lock_guard _(_mx);
        auto iter = fd_map.find(s);
        if (iter != fd_map.end()) {
            std::swap(tmp,iter->second);
            fd_map.erase(iter);
            epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, s, &ev);
        }
        mclosing_map.emplace(s);
    }
}


void Poller_epoll::notify() {
    _ntf_event.regEvent();
}


void Poller_epoll::rearm_fd(bool first_call, FDMap::iterator iter) {
	epoll_event ev ={};
	ev.events = 0;
	ev.data.fd = iter->first;
	RegList &lst = iter->second;
	//const std::chrono::system_clock::time_point maxtm = std::chrono::system_clock::time_point::max();
	lst.timeout = std::chrono::system_clock::time_point::max();

	for (const Reg &x: lst) if (x.cb) {
			lst.timeout = std::min(lst.timeout, x.timeout);
			Op op = static_cast<Op>(std::distance<const Reg *>(lst.data(),&x));
			switch (op) {
                case Op::read:
			    case Op::accept: ev.events |= EPOLLIN; break;
                case Op::connect:
				case Op::write: ev.events |= EPOLLOUT; break;
				default:break;
			}
	}
	if (ev.events) {
		ev.events |= EPOLLONESHOT;
		int r = epoll_ctl(_epoll_fd, first_call?EPOLL_CTL_ADD:EPOLL_CTL_MOD, ev.data.fd, &ev);
		if (r < 0) {
			int e = errno;
			throw std::system_error(e, std::generic_category(), "epoll_ctl");
		}

		if (lst.timeout < first_timeout) {
		    first_timeout = lst.timeout;
		    notify();
		}
	}
}


std::chrono::system_clock::time_point Poller_epoll::clear_timeouts(PendingNotify &spt, std::chrono::system_clock::time_point now) {
    //performing full row scan to search timeouted descriptor
    FDMap::iterator iter = fd_map.begin();
    //longest timeout is max
    auto max = std::chrono::system_clock::time_point::max();
    //until end of map reached
    while (iter != fd_map.end()) {
        //the registration contains nearest timeout for all events
        //if reached
        if (iter->second.timeout < now) {
            //try to find valid event for thi registration
            for (auto &x: iter->second) if (x.cb) {
                //any timeout event is resolved
                if (x.timeout < now) {
                    spt.push_back(x.cb(false));
                }
            }
            //rearm the descriptor according new state
            rearm_fd(false, iter);
            //rearm shoudl update iter->second.timeout
        }
        //calculate nearest timeout point
        max = std::min(max,iter->second.timeout);
        //move next item
        ++iter;
    }
    //return next timeout point
    return max;
}


void Poller_epoll::worker() noexcept {
    std::unique_lock lock(_mx);

    try {
        auto now = std::chrono::system_clock::now();

        epoll_event events[16];
        int r;

        do {
            int timeout = -1;
            auto intr = _current_scheduler->get_idle_interval();
            auto xtm = std::min(intr, first_timeout);
            //if not, we can continue in blocking operation
            if (xtm == std::chrono::system_clock::time_point::max()) {
                timeout = -1;
            } else if (xtm < now) {
                timeout = 0;
            } else {
                timeout = std::chrono::duration_cast<std::chrono::milliseconds>(xtm - now).count();
            }
            lock.unlock();
            r = epoll_wait(_epoll_fd, events, 16, timeout);
            if (r < 0) {
                int e = errno;
                if (e != EINTR) {
                    throw std::system_error(e, std::generic_category(), "epoll_wait");
                }
            }
            lock.lock();
        } while (r < 0);
        now = std::chrono::system_clock::now();

        ntf.clear();

        if (r == 0) {
            first_timeout = clear_timeouts(ntf, now);
        } else {
            for (int i = 0; i < r; i++) {
                auto &e = events[i];
                int fd = e.data.fd;
                if (fd != _ntf_event.getFD()) {
                    auto iter = fd_map.find(fd);
                    if (iter != fd_map.end()) {
                        RegList &regs = fd_map[fd];
                        if (e.events & EPOLLERR) {
                            for (auto &x: regs) {
                                //any error on socket
                                //is reported as complete
                                //because the error is also reported during futher socket operation
                                ntf.push_back(x.cb(true));
                            }
                        }
                        if (e.events & EPOLLIN) {
                            auto &xa = regs[static_cast<int>(Op::accept)];
                            auto &xr = regs[static_cast<int>(Op::read)];
                            ntf.push_back(xa.cb(true));
                            ntf.push_back(xr.cb(true));
                        }
                        if (e.events & EPOLLOUT) {
                            auto &xc = regs[static_cast<int>(Op::connect)];
                            auto &xw = regs[static_cast<int>(Op::write)];
                            ntf.push_back(xc.cb(true));
                            ntf.push_back(xw.cb(true));
                        }
                        rearm_fd(false, iter);
                    }

                } else {
                    _ntf_event.fetch_and_reset();
                }
            }
        }
        for (auto &x: ntf) {
            if (x) _current_scheduler->schedule(std::move(x));
        }
        if (_request_stop) {
            _running = false;
            _current_scheduler = nullptr;
            lock.unlock();
            _cond.notify_all();
            _end_promise();

        } else {
            _current_scheduler->schedule([&]{worker();});
        }
    } catch (...) {
        _running = false;
        _current_scheduler = nullptr;
        lock.unlock();
        _cond.notify_all();
        _end_promise.reject();
    }
}


void Poller_epoll::close(SocketHandle handle) {
    shutdown(handle);
    std::lock_guard _(_mx);
    mclosing_map.erase(handle);

}


}
#endif


