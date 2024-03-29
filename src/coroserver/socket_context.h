#pragma once
#ifndef SRC_COROSERVER_SOCKET_CONTEXT_H_
#define SRC_COROSERVER_SOCKET_CONTEXT_H_
#include <memory>
#include <coro.h>
#include <variant>
#include <list>

#include "peername.h"

#include "stream.h"
#include <chrono>
namespace coroserver {


class Context;
class ISocketContext;


class SocketBase {
public:
    using Ident = int;  //it is linux FD, however for different platform can be simulated

    SocketBase(Ident s, PeerName peer_name, std::weak_ptr<ISocketContext> context);

    SocketBase(SocketBase &&other) = default;

    SocketBase &operator=(SocketBase &&other);

    ~SocketBase();

    void shutdown();

    const PeerName &get_peer_name() const {return _peer_name;}

public:
    Ident _s;
    PeerName _peer_name;
    std::weak_ptr<ISocketContext> _context;

};





class ConnectingSocket {
public:
    ConnectingSocket(std::weak_ptr<ISocketContext> context,
            const PeerName &target, std::chrono::system_clock::time_point timeout)
        :_context(std::move(context))
        ,_target(target)
        ,_timeout(timeout) {}

    auto operator co_await() {return connect();}
    operator Stream() {return connect().get();}
    coro::future<Stream> connect();



protected:
    std::weak_ptr<ISocketContext> _context;
    PeerName _target;
    std::chrono::system_clock::time_point _timeout;
    coro::future<bool> _wait_fut;
};


class Server {
public:

    Server(coro::generator<Stream> &&gen, std::stop_source &&stp)
        :_gen(std::move(gen)), _stp(std::move(stp)) {}
    ~Server() {_stp.request_stop();}
    Server(Server &&) = default;
    Server &operator=(Server &&) = delete;

    coro::deferred_future<Stream> accept() {return _gen();}
    auto operator co_await() {return accept();}
    operator Stream() {return accept();}

protected:
    coro::generator<Stream> _gen;
    std::stop_source _stp;
};




class ISocketContext {
public:

    using SchItem = coro::promise<bool>::notify;

    using Ident = Socket::Ident;


    virtual ~ISocketContext() = default;

    virtual void serve(coro::function<void(SchItem)> schedule_fn) = 0;
    virtual void start_thread(coro::function<void(ISocketContext::SchItem)> schedule_fn) = 0;

    virtual Server listen(std::vector<PeerName> &addresses,
                          TimeoutSettings stream_timeouts = defaultTimeout) = 0;

    Server listen(std::vector<PeerName> &&addresses,
                  TimeoutSettings stream_timeouts = defaultTimeout) {
        return listen(addresses, stream_timeouts);
    }

    virtual coro::future<Stream> connect(const std::vector<PeerName> &addresses,
                TimeoutSettings::Dur connect_timeout = defaultConnectTimeout,
                TimeoutSettings stream_timeouts = defaultTimeout) = 0;
    virtual coro::future<Stream> connect(const PeerName &address,
                TimeoutSettings::Dur connect_timeout = defaultConnectTimeout,
                TimeoutSettings stream_timeouts = defaultTimeout) = 0;



    virtual void stop();
protected:

    virtual void shutdown(Ident ident) = 0;
    virtual void close(Ident ident) = 0;



    friend class SocketBase;
    friend class Socket;
    friend class ListeningSocket;
};

class Context {
public:
    Context();
    ~Context();

    Context(Context &&) =default;
    Context &operator=(Context &&) =default;
    Context(const Context &) =delete;
    Context &operator=(const Context &) =delete;

    coro::future<Socket> connect(const PeerName &addr, std::chrono::system_clock::time_point timeout);

    static auto thread_pool(std::shared_ptr<coro::thread_pool> pool) {
        return [pool](ISocketContext::SchItem item) mutable{
            pool->enqueue(std::move(item));
        };
    }

    static auto thread_pool(unsigned int threads) {
        return thread_pool(std::make_shared<coro::thread_pool>(threads));
    }

    using SchItem = coro::promise<bool>::notify;

    ///Run foreground (single threaded)
    void run();

    ///Run control thread foreground, use custom scheduler
    void run(coro::function<void(SchItem)> schedule_fn);

    ///Start thread at background, use custom scheduler
    void start(coro::function<void(SchItem)> schedule_fn);

    ///start using thread pool
    void start(std::shared_ptr<coro::thread_pool> thread_pool);;

    ///start and create thread pool - use specified count threads
    /**
     * @param threads count of threads allocated for the thread pool. Note that
     * there is always one extra thread for control thread
     */
    void start(unsigned int threads);



    ///Creates connection by connecting one of specified addresses
    coro::future<SocketBase> connect(std::vector<PeerName> addresses);

    class Server {
    public:
        coro::deferred_future<SocketBase> operator()();
        Server(coro::generator<SocketBase> gen, std::stop_source stopper);
        ~Server();
    protected:
        coro::generator<SocketBase> _gen;
        std::stop_source stopper;
    };

    ///Listen for new connection on given list of addresses
    Server listen(std::vector<PeerName> &addresses);
    ///Listen for new connection on given list of addresses
    Server listen(std::vector<PeerName> &&addresses);



protected:
    std::shared_ptr<ISocketContext> _ctx;

};




}
#endif /* SRC_COROSERVER_SOCKET_CONTEXT_H_ */
