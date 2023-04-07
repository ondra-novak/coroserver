/*
 * dispatcher_epoll.h
 *
 *  Created on: 27. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_POLLER_EPOLL_H_
#define SRC_USERVER_POLLER_EPOLL_H_

#include "ipoller.h"


#include "scheduler.h"

#include <cocls/thread_pool.h>

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <queue>
#include <unordered_map>
#include <set>
#include <utility>


namespace coroserver {


class Poller_epoll: public IPoller<int> {
public:

    using SocketHandle = int;

	Poller_epoll(cocls::thread_pool &pool);
	virtual ~Poller_epoll() override;

	///wait for read, resolve promise when done
    virtual void async_wait(AsyncOperation op, SocketHandle s, Promise p, std::chrono::system_clock::time_point timeout = std::chrono::system_clock::time_point::max()) override;

    ///cancel read, and all further reads, socket is marked as closed
    virtual cocls::suspend_point<void> mark_closing(SocketHandle s) override;

    ///cancel all io operations - all sockets are marked as closed
    virtual cocls::suspend_point<void> mark_closing_all() override;

    ///a handle has been closed
    virtual void handle_closed(SocketHandle s) override;

    ///timeout wait, use poller as scheduler
    virtual void schedule(const void *ident, Promise p, std::chrono::system_clock::time_point timeout) override;

    ///cancels specified timer
    virtual cocls::suspend_point<bool> cancel_schedule(const void *ident) override;


protected:

    cocls::async<void> worker(cocls::thread_pool &pool);


    using Op = AsyncOperation;

	struct Reg {
		std::chrono::system_clock::time_point timeout;
		Promise cb;
	};

	class RegList: public std::array<Reg,  static_cast<int>(Op::_count)>{
	public:
		std::chrono::system_clock::time_point timeout;
	};

	using FDMap = std::unordered_map<SocketHandle, RegList>;
	using MarkedClosingMap = std::set<SocketHandle>;


	int epoll_fd;
	int event_fd;

	std::mutex _mx;

	FDMap fd_map;
	MarkedClosingMap mclosing_map;
	std::chrono::system_clock::time_point first_timeout;


	cocls::future<void> _running;
	std::atomic<bool> _stopped = false;
	std::atomic<bool> _exit = false;

	void notify();
	void rearm_fd(bool first_call, FDMap::iterator iter);

	std::chrono::system_clock::time_point clear_timeouts(cocls::suspend_point<void> &spt, std::chrono::system_clock::time_point now);


	Scheduler<Promise> _sch;



};

}



#endif /* SRC_USERVER_POLLER_EPOLL_H_ */
