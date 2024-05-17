#pragma once
#ifndef SRC_COROSERVER_ASYNC_ENGINE_EPOLL_H_
#define SRC_COROSERVER_ASYNC_ENGINE_EPOLL_H_

#include "peername.h"
#include "event_fd.h"
#include "async_engine.h"
#include <coro.h>
#include <mutex>
#include <set>
#include <chrono>
namespace coroserver {


template<typename T>
struct CacheFriendlyAllocator {

    using value_type = T;

    union Item {
        T _payload;
        Item *_next_free;

        constexpr Item():_next_free(nullptr) {};
        constexpr ~Item() {};
    };

    std::deque<Item> _items = {};
    Item *_first_free = nullptr;

    CacheFriendlyAllocator() = default;
    CacheFriendlyAllocator(const CacheFriendlyAllocator &) {} //default
    template<typename U>
    CacheFriendlyAllocator(const CacheFriendlyAllocator<U> &) {} //default

    T *allocate(int n) {
       if (n != 1) {
           return reinterpret_cast<T *>(::operator new(sizeof(T)*n));
       }
       if (_first_free) {
           Item *x = _first_free;
           _first_free = x->_next_free;
           return &x->_payload;
       } else {
           _items.emplace_back();
           Item &x = _items.back();
           return &x._payload;
       }
    }

    void deallocate(T *ptr, int n) {
        if (n != 1) {
            ::operator delete(ptr);
            return;
        }
        static constexpr Item tst;
        static auto ofs = reinterpret_cast<const char *>(&tst._payload) - reinterpret_cast<const char *>(&tst);
        Item *x = reinterpret_cast<Item *>(reinterpret_cast<char *>(ptr)- ofs);
        x->_next_free = _first_free;
        _first_free = x;
    }
};

class AsyncEngineImpl {
public:

    AsyncEngineImpl();
    ~AsyncEngineImpl();

    AsyncEngineImpl(const AsyncEngineImpl &) = delete;
    AsyncEngineImpl &operator=(const AsyncEngineImpl &) = delete;


    using Handle = AsyncEngine::Handle;


    using RetVal = coro::future<int>;
    using Promise = coro::promise<int>;
    using Notify = coro::promise<int>::notify;
    using Timepoint = std::chrono::system_clock::time_point;

    static Handle connect(const PeerName &target);
    static Handle listen(const PeerName &ifc);
    static Handle from_fd(int fd);

    static Handle connect_special(SpecialDevice dev);
    static Handle create_named_pipe(OperationMode mode, std::string name);
    static Handle open_file(OperationMode mode, std::string name, bool append);
    static Handle install_intr_signal();



    RetVal recv(Handle h, void *buffer, std::size_t size, Timepoint timeout);
    unsigned int recv_nb(Handle h, void *buffer, std::size_t size);
    RetVal send(Handle h, const void *buffer, std::size_t size, Timepoint timeout);
    RetVal wait_connect(Handle h, Timepoint timeout);
    RetVal accept(Handle h, Handle &retHandle, PeerName &retPeerName, Timepoint timeout);
    static void send_eof(Handle h);
    void close_handle(Handle h);
    void block(Handle h, bool blocked);

    Notify wait_for_next_event(Timepoint timeout);
    Notify wait_for_next_event();
    void cancel_wait_for_next_event();
    static int get_siocoutq(Handle h);
    PeerName get_name(Handle h);

    void block_all(bool block);


protected:

    static constexpr Timepoint maxtp = Timepoint::max();

    struct InfoBase {
        Promise _prom = {};
        Timepoint _tp = maxtp;
        InfoBase ():_prom(),_tp(maxtp) {} //GCC-13 bug prevent default
    };

    struct InfoEmpty: InfoBase {};

    struct AcceptInfo: InfoBase {
        Handle *_handle = nullptr;
        PeerName *_peerName = nullptr;
    };

    struct ConnectInfo: InfoBase {
    };

    struct RecvInfo: InfoBase {
        void *_buffer = nullptr;
        std::size_t _buffer_size = 0;
    };

    struct SendInfo: InfoBase {
        const void *_buffer = nullptr;
        std::size_t _buffer_size = 9;
    };

    struct ConnectNamedPipeInfo: InfoBase {
    };


    class SocketReg : public AsyncResource {
    public:

        SocketReg (FileDescriptor socket):_socket(std::move(socket)) {}

        ///associated socket
        FileDescriptor _socket;

        ///blocked async io
        bool _blocked = false;
        ///this is pipe, some operations are wired differently
        bool _pipe = false;
        ///eof detected, futher reads returns 0 immediatelly
        bool _eof = false;

        ///current timeout - as registered in timeout map;
        Timepoint _timeout;
        std::variant<InfoEmpty, AcceptInfo, RecvInfo, ConnectNamedPipeInfo> _recv_state {};
        std::variant<InfoEmpty, ConnectInfo, SendInfo, ConnectNamedPipeInfo> _send_state {};
        static SocketReg &from_handle(Handle h);
    };

    struct TimeoutMapCmp {
        bool operator()(SocketReg *a, SocketReg *b) const {
            return (a->_timeout < b->_timeout)
                    || (a->_timeout == b->_timeout && a < b);
        }
    };

    using TimeoutMap = std::set<SocketReg *, TimeoutMapCmp, CacheFriendlyAllocator<SocketReg *> >;
    TimeoutMap _tm_map;

    FileDescriptor _epoll;
    EFDEventRegister _notify;
    std::mutex _mx;
    bool _blocked_all = false;
    std::queue<Notify> _ready;
    std::vector<std::unique_ptr<SocketReg> > _to_free;

    Timepoint _next_wakeup = {};

    void update_socket(SocketReg &reg);
    bool update_timeout(SocketReg &reg);
    bool insert_timeout(SocketReg &reg);
    bool is_blocked(SocketReg &reg);

    static void install_intr_signal_impl();
    static void signal_handler(int);
};



}



#endif /* SRC_COROSERVER_ASYNC_ENGINE_EPOLL_H_ */
