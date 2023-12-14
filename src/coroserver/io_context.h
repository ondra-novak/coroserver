/*
 * socket_context.h
 *
 *  Created on: 2. 10. 2022
 *      Author: ondra
 */

#ifndef SRC_USERVER_IO_CONTEXT_H_
#define SRC_USERVER_IO_CONTEXT_H_
#include "async_support.h"
#include "defs.h"
#include "ipoller.h"
#include "stream.h"
#include "peername.h"

#include <coro.h>
#include <functional>

#include <stop_token>

using coroserver::PeerName;

#ifndef COROSERVER_DEFAULT_TIMEOUT
#define COROSERVER_DEFAULT_TIMEOUT 60000
#endif

namespace coroserver {


class ContextIOImpl: public IAsyncSupport, public std::enable_shared_from_this<ContextIOImpl> {
public:

    using AcceptResult = std::pair<SocketHandle, PeerName>;


    ContextIOImpl(coro::scheduler &sch);
    ContextIOImpl(std::unique_ptr<coro::scheduler> sch);
    ContextIOImpl(std::size_t iothreads);
    ~ContextIOImpl();

    virtual WaitResult io_wait(SocketHandle handle,
                     AsyncOperation op,
                     std::chrono::system_clock::time_point timeout) override;
    virtual void shutdown(SocketHandle handle)  override;
    virtual void close(SocketHandle handle)  override;

    void stop();

    coro::scheduler &get_scheduler()  {
        return *_scheduler;
    }


protected:

    using SchDeleter = void (*)(coro::scheduler *sch);
    using SchPtr = std::unique_ptr<coro::scheduler, SchDeleter>;


    SchPtr _scheduler;
    std::unique_ptr<IPoller> _disp;
    coro::future<void> _disp_run;
};




///context IO
/**
 * Contains common variables need to support network connections
 *
 * The context reference is shared by various object created by this object. You
 * can only create the context and it is kept alive if there is at least one
 * reference
 *
 * The context must be started before can be used. To destroy context, you need to
 * stop the context and remove all connections. By stopping context, all pending IO
 * operations are canceled
 *
 */
class ContextIO {
public:

    static constexpr int defaultTimeout = COROSERVER_DEFAULT_TIMEOUT;

    ContextIO(std::shared_ptr<ContextIOImpl> ptr):_ptr(std::move(ptr)) {}

    ///create socket context
    /**
     * @param dispatcherCount count dispatchers. In most of cases, 1 is enough, but
     * if you expect a lot of connections, you can increase count of dispatchers. Each
     * dispatcher allocates one thread
     *
     * @return instance (shared)
     *
     * @note before using the context, you need to start it and associate it with
     * a thread pool. See start()
     *
     * @see start
     */
    static ContextIO create(coro::scheduler &sch);
    static ContextIO create(std::unique_ptr<coro::scheduler> sch);
        static ContextIO create(std::size_t iothreads = 2);


    coro::scheduler &get_scheduler()  {
        return _ptr->get_scheduler();
    }


    ///Create listening socket at given peer
    AsyncSocket listen_socket(const PeerName &addr);
    ///Create connected socket. Connection is asynchronous, you need to check status of socket
    AsyncSocket create_connected_socket(const PeerName &addr);



    ///Create accept generator
    /**
     * Accept generator opens one or more ports at given addresses,
     * and starts listening on it. Each generator call returns coro::future which
     * is resolved by connected stream.
     *
     * The listening can be stopped by one of following ways. You can use
     * supplied stop token, or you can stop whole context, which also stops the generator
     *
     * @param list list of addresses. The list is carried as lvalue reference and can be
     *  modified by the function. This is useful, when PeerName refers to random opened
     *  port. When function returns, apropriate PeerName is updated with real port number
     *
     * @param token stop token to stop generator. Stop can be requested anytime, regadless on
     * which state is the generator
     *
     * @param tms timeouts sets on resulting stream
     *
     * @return generator
     */
    coro::generator<Stream> accept(
            std::vector<PeerName> &list,
            std::stop_token token = {},
            TimeoutSettings tms = {defaultTimeout,defaultTimeout});

    ///Create accept generator
    /**
     * Accept generator opens one or more ports at given addresses,
     * and starts listening on it. Each generator call returns coro::future which
     * is resolved by connected stream.
     *
     * The listening can be stopped by one of following ways. You can use
     * supplied stop token, or you can stop whole context, which also stops the generator
     *
     * @param list list of addresses. The list is carried as rvalue reference and can be
     *  modified by the function.
     *
     * @param token stop token to stop generator. Stop can be requested anytime, regadless on
     * which state is the generator
     *
     * @param tms timeouts sets on resulting stream
     *
     * @return generator
     */
    coro::generator<Stream> accept(
            std::vector<PeerName> &&list,
            std::stop_token token = {},
            TimeoutSettings tms = {defaultTimeout,defaultTimeout});


    ///Connect stream to one of given addresses

    coro::future<Stream> connect(std::vector<PeerName> list, int timeout_ms = defaultTimeout, TimeoutSettings tms = {defaultTimeout,defaultTimeout});



    ///Stop the running context
    /**
     * - marks all connections closed
     * - disables io_wait, it always resolves connection as closed
     * - stops any accept-generator
     * - resumes all suspended coroutines waiting for io with an error state
     *
     * Internally the context and associated thread pool is left running to allows coroutines
     * to finish their work. You need to join all pending coroutines to ensure, that
     * everything is stopped. The context itself is destroyed once all references are removed
     */
    void stop();


    ///Retrieve stop function
    /**
     * @return returns a function which once called, the context is stopped.
     * @see stop();
     */
    auto stop_fn() {
        return [ctx = *this]() mutable {
            ctx.stop();
            ctx._ptr.reset();
        };
    }


    protected:
      std::shared_ptr<ContextIOImpl> _ptr;


    ///Create accept generator
    /**
     * Accept generator listen new connections and returns them. You need to read
     * the generator in cycle to accept new connections. See description of generator
     * object. The generator can be stopped.
     *
     * @param list list of addresses to listen.
     * @param token stop token
     * @param tms timeouts for newly created streams
     * @return generator
     */


public:
    template<std::invocable<Stream> Fn>
    coro::future<void> tcp_server(Fn &&main_fn, std::vector<PeerName> lsn_peers,
            std::stop_token stoptoken = {},
            TimeoutSettings tms = {defaultTimeout, defaultTimeout}) {
        auto gen = accept(std::move(lsn_peers),stoptoken, tms);
        auto fn = [](coro::generator<Stream> gen, Fn main_fn) -> coro::async<void> {
            auto f = gen();
            while (co_await f.has_value()) {
                main_fn(std::move(f.get()));
                f = gen();
            }
        };
        return fn(std::move(gen), std::move(main_fn));
    }
};


///Declaration of factor, which is able to create connection to given host:port
using ConnectionFactory = std::function<coro::future<Stream>(std::string_view)>;


}




#endif /* SRC_USERVER_IO_CONTEXT_H_ */
