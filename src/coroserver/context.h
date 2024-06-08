/*
 * socket_context.h
 *
 *  Created on: 2. 10. 2022
 *      Author: ondra
 */

#ifndef SRC_USERVER_IO_CONTEXT_H_
#define SRC_USERVER_IO_CONTEXT_H_
#include "defs.h"
#include "stream.h"
#include "peername.h"

#include <coro.h>

#include "async_engine.h"

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

    ///create dorman context
    /**
     * Context can't handle asynchronous operations. You need to call start()
     */
    Context();
    ///create context and start n-threads
    explicit Context(std::size_t iothreads);

    ~Context();


    template<coro::awaitable Awt>
    auto start(Awt &&awt)  {

        auto stop_coro = [&]()->coro::future<coro::awaitable_result<Awt> > {
            co_return co_await awt;
        };
        coro::future<coro::awaitable_result<Awt> > r = stop_coro();
        _scheduler.run(r);
        return r.await_resume();
    }

    template<coro::awaitable Awt>
    auto start(Awt &&awt, std::size_t iothreads)  {

        auto stop_coro = [&]()->coro::future<coro::awaitable_result<Awt> > {
            co_return co_await awt;
        };
        coro::future<coro::awaitable_result<Awt> > r = stop_coro();

        _tpool.emplace(iothreads);
        _scheduler.run(r,[&](auto &&cb){
            _tpool->enqueue(std::move(cb));
        });
        return r.await_resume();

    }

    ///stop context
    /**
     * Stops context canceling all asynchronous operations. The context is not destroyed,
     * however it can no longer be used to perform async IO operations. All opened conections
     * are reported as closed by peer. Some service can throw an exception
     * coro::await_canceled_exception.
     *
     * This function is useful when you need to cleanup any still opened connection to
     * ensure context termination. However this doesn't garantee that termination
     * happens, it only disables blocking.
     */
    void stop();


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
     * @note function is not MT Safe.
     */
    Stream create_intr_listener();


    ///Create bidirectional stream pair
    std::pair<Stream, Stream> create_pair(TimeoutSettings tms = {});


protected:

    struct CondVar {
        Context *_owner;
        void notify_all();
        void wait_until(std::unique_lock<std::mutex> &lk, std::chrono::system_clock::time_point tp);
        void wait(std::unique_lock<std::mutex> &lk);
    };


    std::optional<coro::thread_pool> _tpool;
    AsyncEngine _engine;
    coro::scheduler_t<CondVar> _scheduler;






public:

    template<typename Fn>
    class TCPServer: public coro::future<void> {
    public:
        TCPServer(Context &ctx, Fn &&fn, std::vector<PeerName> &lsn_peers,
                TimeoutSettings tms, std::stop_token stp)
            :_main_fn(std::forward<Fn>(fn))
            ,_gen(ctx.accept(lsn_peers, stp, tms)) {
            _fin = get_promise();
            charge();
        }


    protected:
        Fn _main_fn;
        coro::generator<Stream> _gen;
        coro::deferred_future<Stream> _fut;
        coro::promise<void> _fin;

        void charge() {
            _fut = _gen();
            _fut >> [this] {
                try {
                    if (_fut.has_value()) {
                        _main_fn(std::move(_fut.get()));
                        charge();
                    } else {
                        _fin();
                    }
                } catch (...) {
                    _fin.reject();
                }
            };
        }
    };

    template<std::invocable<Stream> Fn>
    auto tcp_server(Fn &&fn, std::vector<PeerName> &lsn_peers, std::stop_token stp = {}, TimeoutSettings tms = defaultTimeout) {
        return TCPServer<Fn>(*this, std::forward<Fn>(fn), lsn_peers, tms, stp);
    }
    template<std::invocable<Stream> Fn>
    auto tcp_server(Fn &&fn, std::vector<PeerName> &&lsn_peers, std::stop_token stp = {}, TimeoutSettings tms = defaultTimeout) {
        return TCPServer<Fn>(*this, std::forward<Fn>(fn), lsn_peers, tms, stp);
    }

};




///Declaration of factor, which is able to create connection to given host:port
using ConnectionFactory = std::function<coro::future<Stream>(std::string_view)>;


}




#endif /* SRC_USERVER_IO_CONTEXT_H_ */
