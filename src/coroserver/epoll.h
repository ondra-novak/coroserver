#pragma once
#ifndef SRC_COROSERVER_EPOLL_H_
#define SRC_COROSERVER_EPOLL_H_
#include "event_fd.h"

#include <coro.h>

#include "reuse_allocator.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>


namespace coroserver {

enum class Operation {
    input = 0,
    output,
};



class AsyncEPoll {

    struct __blocker {
        const void *ident;
        void operator()(AsyncEPoll *ptr) {ptr->unblock(ident);}
    };

public:



    AsyncEPoll();

    AsyncEPoll(const AsyncEPoll &) = delete;
    AsyncEPoll &operator=(const AsyncEPoll &) = delete;


    coro::future<bool> wait(int fd, Operation op, std::chrono::system_clock::time_point timeout);
    coro::future<bool> wait(std::chrono::system_clock::time_point timeout, const void *ident = nullptr);
    void shutdown(int fd);
    void unreg(int fd);
    std::unique_ptr<AsyncEPoll, __blocker> cancel(const void *ident);

    template<std::invocable<coro::promise<bool>::notify> Scheduler>
    void serve(Scheduler &&scheduler, std::stop_token stoken);

    template<std::invocable<coro::promise<bool>::notify> Scheduler>
    std::jthread start_thread(Scheduler &&scheduler);
protected:
    static constexpr auto max_timeout = std::chrono::system_clock::time_point::max();

    struct WaitRegItem {
        coro::promise<bool> _prom = {};
        std::chrono::system_clock::time_point _timeout = {max_timeout};
    };

    struct WaitReg{
        WaitRegItem _items[2];
        bool _shutdown = false;
    };

    struct ScheduledItem{
        std::chrono::system_clock::time_point _tp;
        const void *ident;
        coro::promise<bool> _prom={};
        int operator<=>(const ScheduledItem &other) const {
            return other._tp.time_since_epoch().count() - _tp.time_since_epoch().count();
        }
    };

    using SocketMap = std::unordered_map<int, WaitReg,std::hash<int>, std::equal_to<int>/*,CacheFriendlyAllocator<std::pair<const int, WaitReg> > */>;
    SocketMap _regs = {};
    std::vector<ScheduledItem> _scheduled;
    std::vector<const void *> _blocked;
    FileDescriptor _epoll;
    EFDEventRegister _notify;
    std::mutex _mx;
    std::chrono::system_clock::time_point wakeup_time = max_timeout;
    bool _stopped = {false};


    void register_socket(coro::promise<bool> prom, int fd, Operation op, std::chrono::system_clock::time_point timeout);
    WaitReg do_shutdown(int fd);
    WaitReg do_unreg(int fd);

    bool update_timeout(const WaitReg &reg);
    void update_socket(int fd, const WaitReg &reg);
    void acq_notify();

    void unblock(const void *ident);

    class ICb {
    public:
        virtual void operator()(coro::promise<bool>::notify &&ntf) = 0;
        virtual ~ICb() = default;
    };

    void do_serve(ICb &&scheduler, std::stop_token stoken);
};

template<std::invocable<coro::promise<bool>::notify> Scheduler>
void AsyncEPoll::serve(Scheduler &&scheduler, std::stop_token stoken) {
    class CB: public ICb {
    public:
        CB(Scheduler &&sch):_sch(sch) {}
        virtual void operator()(coro::promise<bool>::notify &&ntf) {
            _sch(std::move(ntf));
        }
    protected:
        std::decay_t<Scheduler> &_sch;
    };

    do_serve(CB(std::forward<Scheduler>(scheduler)), std::move(stoken));
}

template<std::invocable<coro::promise<bool>::notify> Scheduler>
std::jthread AsyncEPoll::start_thread(Scheduler &&scheduler) {
    return std::jthread([this](std::stop_token stoken, auto &&sch){
            serve(std::forward<Scheduler>(sch), std::move(stoken));
    }, std::forward<Scheduler>(scheduler));
}


}



#endif /* SRC_COROSERVER_EPOLL_H_ */
