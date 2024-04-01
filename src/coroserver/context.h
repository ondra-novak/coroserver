/*
 * socket_context.h
 *
 *  Created on: 2. 10. 2022
 *      Author: ondra
 */

#ifndef SRC_USERVER_IO_CONTEXT_H_
#define SRC_USERVER_IO_CONTEXT_H_
#include "timer.h"
#include "defs.h"
#include "ipoller.h"
#include "stream.h"
#include "peername.h"

#include <coro.h>
#include <functional>

#include <stop_token>

using coroserver::PeerName;


namespace coroserver {


class ContextIOImpl;

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
class Context {
public:

    Context();

    Context(Context &&other);
    Context &operator=(Context &&other);


    ~Context();


    using SchItem = coro::promise<bool>::notify;

    ///Run foreground (single threaded)
    void run();

    ///Run control thread foreground, use custom scheduler
    void run(coro::function<void(SchItem)> schedule_fn);

    ///start background thread - single threaded execution
    void start();

    ///Start thread at background, use custom scheduler
    void start(coro::function<void(SchItem)> schedule_fn);

    ///start using thread pool
    void start(std::shared_ptr<coro::thread_pool> thread_pool);

    ///start and create thread pool - use specified count threads
    /**
     * @param threads count of threads allocated for the thread pool. Note that
     * there is always one extra thread for control thread
     */
    void start(unsigned int threads);


    ///start context in foregrount, stop it, when future is resolved
    /**
     * @param fut future to test
     * @param schedule_fn function called to schedule an action. You can use thread_pool scheduler
     * @return result of future
     */
    template<typename T, std::invocable<SchItem> Fn>
    T run_until(coro::future<T> &fut, Fn &&schedule_fn) {
        if (fut.set_callback([&]{stop();})) {
            run(std::forward<Fn>(schedule_fn));
        }
        return fut.get();

    }
    ///start context in foregrount, stop it, when future is resolved
    /**
     * @param fut future to test
     * @param schedule_fn function called to schedule an action. You can use thread_pool scheduler
     * @return result of future
     */
    template<typename T, std::invocable<SchItem> Fn>
    T run_until(coro::future<T> &&fut, Fn &&schedule_fn) {
        return run_until(fut, std::forward<Fn>(schedule_fn));
    }

    ///start context in foregrount, stop it, when future is resolved
    /**
     * @param fut future to test
     * @return result of future
     */
    template<typename T>
    T run_until(coro::future<T> &fut) {return run_until(fut, [](auto){});}
    ///start context in foregrount, stop it, when future is resolved
    /**
     * @param fut future to test
     * @return result of future
     */
    template<typename T>
    T run_until(coro::future<T> &&fut) {return run_until(fut);}


    static auto thread_pool(std::shared_ptr<coro::thread_pool> tpool) {
        return [tpool](SchItem item) mutable {tpool->enqueue(std::move(item));};
    }
    static auto thread_pool(unsigned int thread_count) {
        return thread_pool(std::make_shared<coro::thread_pool>(thread_count));
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
            TimeoutSettings tms = defaultTimeout);

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
            TimeoutSettings tms = defaultTimeout);


    ///Connect stream to one of given addresses

    coro::future<Stream> connect(std::vector<PeerName> list,
            TimeoutSettings::Dur timeout_ms = defaultConnectTimeout,
            TimeoutSettings tms = defaultTimeout);



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


    ///Create timer
    /** The timer provides scheduling features, such a sleep_for and sleep_until for coroutines */
    Timer create_timer();

    ///create stream which serves as pipe
    Stream create_pipe(TimeoutSettings tms = {});

    ///create stream which is redirected to stdin and stdout
    Stream create_stdio(TimeoutSettings tms = {});

    ///open named pipe for reading
    Stream read_named_pipe(const std::string &name, TimeoutSettings tms = {});

    ///open named pipe for writing
    Stream write_named_pipe(const std::string &name, TimeoutSettings tms = {});

    ///Creates stream, which receives a data when external interrupt is signaled
    /**
     * The stream can be read (only), content of read data is platform specific. You
     * can await on this stream to wait on interrupt.
     * The interrupt is hardcoded to SIGTERM, SIGINT, SIGHUP, SIGQUIT.
     * Under Windows, it reacts to CTRL+C and CTRL+BREAK and closing console window.
     *
     * @return stream
     *
     * @note Under linux, one byte is carried which contains signal number
     */
    Stream create_intr_listener();


    ///Create bidirectional stream pair
    std::pair<Stream, Stream> create_pair(TimeoutSettings tms = {});


protected:
      std::shared_ptr<ContextIOImpl> _ptr;


public:
    template<std::invocable<Stream> Fn>
    coro::future<void> tcp_server(Fn &&main_fn, std::vector<PeerName> lsn_peers,
            std::stop_token stoptoken = {},
            TimeoutSettings tms = defaultTimeout) {
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
