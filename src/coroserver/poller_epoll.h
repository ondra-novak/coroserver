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

#include <coro.h>


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


class Poller_epoll: public IPoller {
public:

    using SocketHandle = int;

	Poller_epoll();
	virtual ~Poller_epoll() override;

    virtual coro::future<void> start(coro::scheduler &scheduler) override;
    virtual void stop() override;


    virtual WaitResult io_wait(SocketHandle handle,
                               AsyncOperation op,
                               std::chrono::system_clock::time_point timeout) override;
    virtual void shutdown(SocketHandle handle) override;
    virtual void close(SocketHandle handle) override;


protected:

    coro::async<void> worker(coro::scheduler &pool);


    using Op = AsyncOperation;

	struct Reg {
		std::chrono::system_clock::time_point timeout;
		coro::promise<bool> cb;
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
	bool _running = false;
	bool _request_stop = false;
	std::condition_variable _cond;
	coro::scheduler *_current_scheduler = nullptr;


	void notify();
	void rearm_fd(bool first_call, FDMap::iterator iter);

	using PendingNotify = std::vector<coro::future<bool>::pending_notify>;

	std::chrono::system_clock::time_point clear_timeouts(PendingNotify &spt, std::chrono::system_clock::time_point now);



	void check_still_running();

};

}



#endif /* SRC_USERVER_POLLER_EPOLL_H_ */
