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

#include <cocls/thread_pool.h>
#include <cocls/generator.h>

#include <stop_token>

using coroserver::PeerName;

#ifndef COROSERVER_DEFAULT_TIMEOUT
#define COROSERVER_DEFAULT_TIMEOUT 60000
#endif

namespace coroserver {


class ContextIOImpl: public IAsyncSupport {
public:



    using Promise = IPoller<SocketHandle>::Promise;
    using AcceptResult = std::pair<SocketHandle, PeerName>;


    ContextIOImpl(std::shared_ptr<cocls::thread_pool> pool);
    ContextIOImpl(std::size_t iothreads = 0);
    ~ContextIOImpl();

    virtual void close(SocketHandle h) override;
    virtual cocls::suspend_point<void> mark_closing(SocketHandle s) override;
    virtual cocls::future<WaitResult> io_wait(SocketHandle handle,
                                    AsyncOperation op,
                                    std::chrono::system_clock::time_point timeout) override;



    ///stop the context, cancel all waitings, all opened streams are marked as closed
    cocls::suspend_point<void> stop();

    ///Wait for io operation
    /**
     * Returns awaitable object (co_await)
     *
     * @param handle handle to open socket
     * @param op operation
     * @param timeout wait timeout
     * @return awaitable object, which returns WaitResult
     */


    virtual cocls::future<WaitResult> wait_until(std::chrono::system_clock::time_point tp, const void *ident) override;
    template<typename Dur>
    cocls::future<WaitResult> wait_for(Dur duration, const void *ident) {
        return wait_until(std::chrono::system_clock::now()+duration, ident);
    }

    cocls::suspend_point<bool> cancel_wait(const void *ident) override;


    cocls::thread_pool &get_pool() {
        return *_pool;
    }

protected:
    std::shared_ptr<cocls::thread_pool> _pool;
    std::unique_ptr<IPoller<SocketHandle> > _disp;
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
    static ContextIO create(std::shared_ptr<cocls::thread_pool> pool);
    static ContextIO create(std::size_t iothreads = 0);

    ///Create listening socket at given peer
    ListeningSocketHandle listen_socket(const PeerName &addr);
    ///Create connected socket. Connection is asynchronous, you need to check status of socket
    SocketHandle create_connected_socket(const PeerName &addr);

    operator AsyncSupport() const {
        return AsyncSupport(_ptr);
    }

    ///Create accept generator
    /**
     * Accept generator opens one or more ports at given addresses,
     * and starts listening on it. Each generator call returns cocls::future which
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
    cocls::generator<Stream> accept(
            std::vector<PeerName> &list,
            std::stop_token token = {},
            TimeoutSettings tms = {defaultTimeout,defaultTimeout});

    ///Create accept generator
    /**
     * Accept generator opens one or more ports at given addresses,
     * and starts listening on it. Each generator call returns cocls::future which
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
    cocls::generator<Stream> accept(
            std::vector<PeerName> &&list,
            std::stop_token token = {},
            TimeoutSettings tms = {defaultTimeout,defaultTimeout});


    ///Connect stream to one of given addresses

    cocls::future<Stream> connect(std::vector<PeerName> list, int timeout_ms = defaultTimeout, TimeoutSettings tms = {defaultTimeout,defaultTimeout});



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
    cocls::suspend_point<void> stop();


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
    template<typename Fn>
    CXX20_REQUIRES(std::invocable<Fn, Stream>)
    cocls::future<void> tcp_server(Fn &&main_fn, std::vector<PeerName> lsn_peers,
            std::stop_token stoptoken = {},
            TimeoutSettings tms = {defaultTimeout, defaultTimeout}) {
        auto gen = accept(std::move(lsn_peers),stoptoken, tms);
        auto fn = [](cocls::generator<Stream> gen, Fn main_fn) -> cocls::async<void> {
            while (co_await gen.next()) {
                main_fn(std::move(gen.value()));
            }
        };
        return fn(std::move(gen), std::move(main_fn));
    }
};


///Declaration of factor, which is able to create connection to given host:port
using ConnectionFactory = std::function<cocls::future<Stream>(std::string_view)>;


}




#endif /* SRC_USERVER_IO_CONTEXT_H_ */
