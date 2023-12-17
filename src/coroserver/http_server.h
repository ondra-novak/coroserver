#pragma once
#ifndef SRC_COROSERVER_HTTP_SERVER_H_
#define SRC_COROSERVER_HTTP_SERVER_H_

#include "http_server_request.h"
#include "http_stringtables.h"
#include "prefixmap.h"

#include <coro.h>
#include <shared_mutex>
#include <memory>
#include <functional>



namespace coroserver {

namespace ssl {
    class Context;
}

namespace http {


using HandlerReturn = std::variant<
        std::monostate,
        coro::future<void>,
        coro::future<bool>
>;

class HandlerAwaiter {

    struct always_ready: std::suspend_never {
        bool await_suspend(std::coroutine_handle<>)  {
            return false;
        }
    };

    using AWT = std::variant<always_ready, coro::future<void>::value_awaiter, coro::future<bool>::value_awaiter>;
public:

    bool await_ready() const {
        return std::visit([&](auto &x){
           return x.await_ready();
        },_awt);
    }
    auto await_suspend(std::coroutine_handle<> h) {
        return std::visit([&](auto &x) -> bool{
            return x.await_suspend(h);
        },_awt);
    }

    void await_resume() const {
        return std::visit([&](auto &x){
            x.await_resume();
        },_awt);
    }

    HandlerAwaiter(HandlerReturn &r):_awt(std::visit([&](auto &x)->AWT{
        if constexpr(std::is_same_v<std::decay_t<decltype(x)>, std::monostate>) {
            return always_ready();
        } else {
            return x.operator co_await();
        }
    },r)) {}
    HandlerAwaiter(const HandlerAwaiter &) = default;
    HandlerAwaiter &operator=(const HandlerAwaiter &) = delete;

protected:
    AWT _awt;

};

class Handler {
public:

    template<std::invocable<ServerRequest &, std::string_view> Fn>
    Handler(Fn &&fn);
    template<std::invocable<ServerRequest &> Fn>
    Handler(Fn &&fn);
    Handler() = default;

    void operator()(ServerRequest &req, std::string_view vpath, HandlerReturn &ret) const noexcept {
        _ptr->call(req, vpath, ret);
    }

    explicit operator bool() const {return _ptr != nullptr;}

protected:

    class IHandler {
    public:
        virtual void call(ServerRequest &req, std::string_view vpath, HandlerReturn &ret) const noexcept = 0;
        virtual ~IHandler() = default;
    };

    std::shared_ptr<IHandler> _ptr;

};

template<std::invocable<ServerRequest &, std::string_view> Fn>
Handler::Handler(Fn &&fn) {

    class Impl: public IHandler {
    public:
        Impl(Fn &&fn):_fn(std::forward<Fn>(fn)) {}
        virtual void call(ServerRequest &req, std::string_view vpath, HandlerReturn &ret) const noexcept {
            using RetT = decltype(_fn(req,vpath));
            if constexpr(std::is_same_v<RetT, coro::future<void> >) {
                ret.emplace<coro::future<void> >([&]{return _fn(req,vpath);});
            } else if constexpr(std::is_same_v<RetT, coro::future<bool> >) {
                ret.emplace<coro::future<bool> >([&]{return _fn(req,vpath);});
            } else {
                _fn(req,vpath);
                ret.emplace<std::monostate>();
            }
        }
    protected:
        std::decay_t<Fn> _fn;
    };
    _ptr = std::make_shared<Impl>(std::forward<Fn>(fn));
}

template<std::invocable<ServerRequest &> Fn>
Handler::Handler(Fn &&fn) {

    class Impl: public IHandler {
    public:
        Impl(Fn &&fn):_fn(std::forward<Fn>(fn)) {}
        virtual void call(ServerRequest &req, std::string_view , HandlerReturn &ret) const noexcept {
            using RetT = decltype(_fn(req));
            if constexpr(std::is_same_v<RetT, coro::future<void> >) {
                ret.emplace<coro::future<void> >([&]{return _fn(req);});
            } else if constexpr(std::is_same_v<RetT, coro::future<bool> >) {
                ret.emplace<coro::future<bool> >([&]{return _fn(req);});
            } else {
                _fn(req);
                ret.emplace<std::monostate>();
            }
        }
    protected:
        std::decay_t<Fn> _fn;
    };
    _ptr = std::make_shared<Impl>(std::forward<Fn>(fn));
}



enum class TraceEvent {
    ///request is opened for an connection
    open,
    ///request is loaded from input stream
    load,
    ///request is finished, response has been sent
    finish,
    ///connection closed, request closed
    close,
    ///exception reported (is current)
    exception,
    ///request's logger function (custom log message)
    logger
};



class MethodMap {
public:

    MethodMap() = default;
    void set(Method m, Handler h) {
        methods[static_cast<int>(m)] = h;
    }
    void set(std::initializer_list<Method> m, Handler h) {
        for (auto x: m) set(x,h);
    }
    Handler get(Method m) const {return methods[static_cast<int>(m)];}
    void clear() {
        std::fill(methods.begin(), methods.end(), Handler());
    }

    std::size_t allowed_bitvector() const {
        std::size_t ret = 0;
        for (int i = 0; i < static_cast<int>(Method::unknown); ++i) {
            if (methods[i]) ret |= (1<<i);
        }
        return ret;
    }

    static std::string allowed_to_string(std::size_t bitvector) {
        std::string res;
        int i = 1;
        while(i < static_cast<int>(Method::unknown)) {
            if (bitvector & (1<<i)) {
                res.append(strMethod[static_cast<Method>(i)]);
                ++i;
                while(i < static_cast<int>(Method::unknown)) {
                    if (bitvector & (1<<i)) {
                        res.append(", ");
                        res.append(strMethod[static_cast<Method>(i)]);
                    }
                    ++i;
                }
                break;
            }
            ++i;
        }
        return res;

    }


protected:
    std::array<Handler,static_cast<int>(Method::unknown)+1> methods;
};

///Base routing, base class for Server
/**
 * You can create additional routing tables for cascade routing
 */
class Router {
public:

    ///Register a handler to a given path
    /**
     * @param path Path to register. Note that path is always starts with /. To register
     * custom error page, use error_<code> to handle custom error page handler
     * @param h handler. Set empty to remove handler
     *
     * @note It registers handler for all methods.
     */
    void set_handler(std::string_view path, Handler h);
    ///Register a handler to given path and method
    /**
     * @param path Path to register. Note that path is always starts with /. To register
     * custom error page, use error_<code> to handle custom error page handler
     * @param m method
     * @param h handler. Set empty to remove handler
     */
    void set_handler(std::string_view path, Method m, Handler h);
    ///Register a handler to given path and method
    /**
     * @param path Path to register. Note that path is always starts with /. To register
     * custom error page, use error_<code> to handle custom error page handler
     * @param m method
     * @param h handler. Set empty to remove handler
     */
    void set_handler(std::string_view path, std::initializer_list<Method> methods, Handler h);


    ///Calls handler for given request
    /**
     * @param req request
     * @param fut reference to future to resolve once the handler is finished.
     * @retval 0 handler called and accepted the request
     * @retval 1 handler was not found
     * @retval >1 found handler, but for different method. The value contains bitmask of all found methods (bit 0 is always set)
     */
    std::size_t call_handler(ServerRequest &req, HandlerReturn &fut);
    ///Calls handler for given request
    /**
     * @param req request
     * @param vpath  overrides path (virtual path) - this path is used for lookup
     * @param fut reference to future to resolve once the handler is finished.
     * @retval 0 handler called and accepted the request
     * @retval 1 handler was not found (or handler rejected the request)
     * @retval >1 found handler, but for different method. The value contains bitmask of all found methods (bit 0 is always set)
     */
    std::size_t call_handler(ServerRequest &req, std::string_view vpath, HandlerReturn &fut);
    ///Calls handler for given request
    /**
     * @param req request
     * @param methodOverride - allows to override method and call handler for different method. This potentially useful
     *              to call GET handler to generate response after the POST/PUT body has been processed.
     * @param vpath  overrides path (virtual path) - this path is used for lookup
     * @param fut reference to future to resolve once the handler is finished.
     * @retval 0 handler called and accepted the request
     * @retval 1 handler was not found (or handler rejected the request)
     * @retval >1 found handler, but for different method. The value contains bitmask of all found methods (bit 0 is always set)
     */
    std::size_t call_handler(ServerRequest &req, Method methodOverride, std::string_view vpath, HandlerReturn &fut);

protected:
    PrefixMap<MethodMap> _endpoints;

};


class Server: protected Router {
public:


    using RequestFactory = std::function<ServerRequest(Stream)>;


    ///Create secure request (implements https)
    /**
     * @param ctx SSL context, which should have loaded a certificate.
     * @param group_id specifies group_id for connections which are considered as secure. Default value 0
     * disables filtering, so all incomming connections are considered secure (https). You can define
     * group of addesses with different group_id and mark connections to this addresses as secure.
     * @param mask specifes, that group_id is mask. The connection is considered secure when its
     * peer_name's group_id masked by this mask equals the mask (id & mask == mask). If this value is false,
     * group id is compared for equality
     * @return a function, which is passed to the constructor of the server - servers as request factory
     */
    static RequestFactory secure(ssl::Context &ctx, int group_id = 0, bool mask = false);

    ///Initialize the server with request factory
    /**
     * @param factory function which pre-process the request. You can pass
     * a value Server::secure to make secure server. This assumes https
     * as protocol. However you can perform any complex preprocessing
     * if you pass a custom function, which returns ServerRequest as result
     *
     */
    Server(RequestFactory factory):_factory(std::move(factory)) {}
    Server() = default;


    static std::string_view error_handler_prefix;


    ///Start the server (serve requests)
    /**
     * @param tcp_server instance of tcp server, which is generator of
     * connections. You probably need to pass the argument as rvalue. You
     * can create server by calling ContextIO::accept().
     * @param tracer Object which handles request tracing. It is callable, which
     * is called with arguments TraceEvent and ServerRequest &. You can use this
     * object to add HTTP log. For convenience, there is object DefaultLogger, which
     * handles basic logging for small projects/
     *
     * @return Returns future which is resolved after server stops. To
     * stop server, you need to stop the tcp_server (the generator passed as
     * argument). The server stops serving, once the generator returns without
     * a value. You should wait for this future to ensure, that server is
     * clean up to be destroyed.
     */
    template<std::invocable<TraceEvent, ServerRequest &> Tracer>
    coro::future<void> start(coro::generator<Stream> tcp_server, Tracer tracer) {
        return [&](coro::promise<void> prom) {
            _exit_promise = std::move(prom);
            serve_gen(std::move(tcp_server), std::move(tracer)).detach();
        };
    }

    ///Start the server
    /**
     *
     * @param tcp_server instance of tcp server, which is generator of
     * connections. You probably need to pass the argument as rvalue. You
     * can create server by calling ContextIO::accept().
     * @return Returns future which is resolved after server stops. To
     * stop server, you need to stop the tcp_server (the generator passed as
     * argument). The server stops serving, once the generator returns without
     * a value. You should wait for this future to ensure, that server is
     * clean up to be destroyed.
     */
    coro::future<void> start(coro::generator<Stream> tcp_server) {
        return start(std::move(tcp_server),[](TraceEvent, ServerRequest &) {});
    }

    ///Manually serve on given connection
    /**
     * @param s stream to be handled by the server
     * @return future which is resolved once the connection is closed
     *
     */
    coro::future<void> serve_req(Stream s) {
        return serve_req_coro(std::move(s), [](TraceEvent, ServerRequest &) {});
    }
    ///Manually serve on given connection
    /**
     * @param s stream to be handled by the server
     * @param tracer see start()
     * @return future which is resolved once the connection is closed
     *
     */
    template<typename Tracer>
    coro::future<void> serve_req(Stream s, Tracer tracer) {
        return serve_req_coro(std::move(s), std::move(tracer));
    }

    void set_handler(std::string_view path, Handler h) {
        std::unique_lock lk(_mx);
        Router::set_handler(path, std::move(h));
    }
    void set_handler(std::string_view path, Method m, Handler h) {
        std::unique_lock lk(_mx);
        Router::set_handler(path, m, std::move(h));
    }
    void set_handler(std::string_view path, std::initializer_list<Method> methods, Handler h) {
        std::unique_lock lk(_mx);
        Router::set_handler(path, methods, std::move(h));
    }


protected:
    RequestFactory _factory;
    std::shared_mutex _mx;
    PrefixMap<MethodMap> _endpoints;
    coro::promise<void> _exit_promise;
    std::atomic<int> _requests = 0;

    friend class std::lock_guard<Server>;

    void lock() {
        ++_requests;
    }

    void unlock() {
        if ((--_requests) == 0)
            _exit_promise();
    }


    template<typename Tracer>
    coro::async<void> serve_gen(coro::generator<Stream> tcp_server, Tracer tracer) {
        std::lock_guard _(*this);
        auto v = tcp_server();
        while (co_await v.has_value()) {
            serve_req_coro(std::move(v.get()), tracer).detach();
            v = tcp_server();
        }
        co_return;
    }

    template<typename Tracer>
    static void setup_logger(ServerRequest &req, Tracer &tracer) {
        auto &logger = req.get_logger_info();
        logger.log_fn = [](ServerRequest &req, void *user_ctx) {
            Tracer &trc = *reinterpret_cast<Tracer *>(user_ctx);
            trc(TraceEvent::logger, req);
        };
        logger.user_ctx = &tracer;
    }

    template<typename Tracer>
    coro::async<void> serve_req_coro(Stream s, Tracer tracer) {
        //prepare server request
        ServerRequest req = _factory?_factory(std::move(s)):ServerRequest(std::move(s));

        try {
            //lock this object - count request - this is called in context of serve()
            std::lock_guard _(*this);
            //report that request has been opened
            tracer(TraceEvent::open, req);
            //enable and setup request's logger to the tracer
            setup_logger(req, tracer);

            //load requests from the stream - return false if error
            while (co_await req.load()) {
                //future to await handler
                HandlerReturn fut;
                try {
                    //report that request has been loaded
                    tracer(TraceEvent::load, req);
                    //select matching handler and call it, set future with result
                    select_handler(req, fut);
                    //await for future
                    co_await HandlerAwaiter(fut);
                    //handler can optionally not send the request
                    //if the request is error page
                    //if headers was sent - so request is complete
                    if (req.headers_sent()) {
                        //report that request is complete
                        tracer(TraceEvent::finish, req);
                        //close request if keep alive is not active
                        if (!req.keep_alive()) {
                            //report closed
                            tracer(TraceEvent::close, req);
                            //exit
                            co_return;
                        }
                        //keep alive is active, load next request
                        continue;
                    }
                    //here if the response was not send
                } catch (...){
                    //in case of exception
                    tracer(TraceEvent::exception, req);
                    //exception has been thrown after response has been sent
                    //we has no idea in which state this happened
                    //so the best solution is to close the connection
                    if (req.headers_sent()) {
                        //report that request is being closed
                        tracer(TraceEvent::close, req);
                        //exit now - connection will be closed
                        co_return;
                    }
                    //exception was thrown during processing the request
                    //before response has been sent
                    //so set status to 500
                    req.set_status(500);
                    //clear any headers
                    req.clear_headers();
                }
                //we are here, when request is processed, but response was not sent
                //so explore status and generate error page
                send_error_page(req, fut);
                co_await HandlerAwaiter(fut);
                //report finish request
                tracer(TraceEvent::finish, req);
                //if keep alive isn't active
                if (!req.keep_alive()) {
                    //report closed
                    tracer(TraceEvent::close, req);
                    //exit
                    co_return;
                }
                //load next request
            }
            //in this case, load fails
            //but status can be set indicating that error page should be returned to the client
            if (req.get_status()) {
                //this is considered as load.
                tracer(TraceEvent::load, req);
                //send error page
                HandlerReturn fut;
                send_error_page(req, fut);
                co_await HandlerAwaiter(fut);
                //and report finish
                tracer(TraceEvent::finish, req);
                //keep alive is impossible here
            }
            //so report close
            tracer(TraceEvent::close, req);
            //and exit
            co_return;
        } catch (...) {
            tracer(TraceEvent::exception, req);
            tracer(TraceEvent::close, req);
            co_return;
        }
    }

    void send_error_page(ServerRequest &req, HandlerReturn &fut);
    void select_handler(ServerRequest &req, HandlerReturn &fut);
};

template<typename Output>
class DefaultLogger {


    struct Content {
        std::mutex _mx;
        std::size_t _counter = 0;
        Output _output;
        Content (Output &&out):_output(out) {}
        void send_out(std::string_view text) {
            std::lock_guard _(_mx);
            _output(text);
        }
        std::size_t get_ident() {
            std::lock_guard _(_mx);
            return ++_counter;
        }
    };

public:

    void operator()(TraceEvent ev, ServerRequest &req) {
        _buffer.clear();
        _buffer.push_back('[');
        _buffer.append(std::to_string(_ident));
        _buffer.push_back(']');
        _buffer.push_back(' ');
        std::size_t counter =0;
        switch (ev) {
            case TraceEvent::open: _buffer.append("New connection");break;
            case TraceEvent::load: _start_time = std::chrono::system_clock::now();
                                   _counter = req.get_counters().write;
                                   return;
            case TraceEvent::exception:
            case TraceEvent::finish: _buffer.append(strMethod[req.get_method()]);
                                     _buffer.push_back(' ');
                                     _buffer.append(req.get_url());
                                     _buffer.push_back(' ');
                                     _buffer.append(std::to_string(req.get_status()));
                                     _buffer.push_back(' ');
                                     if (ev == TraceEvent::exception) {
                                         try {
                                             throw;
                                         } catch (std::exception &e) {
                                             _buffer.append(e.what());
                                             _buffer.push_back(' ');
                                         }
                                     }
                                     counter = req.get_counters().write - _counter;
                                     _buffer.append(std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()- _start_time).count()));
                                     _buffer.append(" ms, ");
                                     _buffer.append(std::to_string((counter+512)/1024));
                                     _buffer.append(" KiB");
                                     break;
            case TraceEvent::close: _buffer.append("closed. Read: ");
                                    {
                                        auto cntr = req.get_counters();
                                        _buffer.append(std::to_string((cntr.read+512)/1024));
                                        _buffer.append(" KiB / Write: ");
                                        _buffer.append(std::to_string((cntr.write+512)/1024));
                                        _buffer.append(" KiB");

                                    }
                                    break;
            case TraceEvent::logger: {
                auto &logger = req.get_logger_info();
                _buffer.append("LOG: <");
                _buffer.append(std::to_string(logger.serverity));
                _buffer.append("> ");
                _buffer.append(logger.message);
                _buffer.append(" - ");
                break;
            }

        }
        _ctx->send_out(_buffer);
    }


    DefaultLogger(Output output):_ctx(std::make_shared<Content>(std::forward<Output>(output)))
                                ,_ident(_ctx->get_ident()) {}
    DefaultLogger(const DefaultLogger &lg):_ctx(lg._ctx)
                                          ,_ident(_ctx->get_ident()) {}
    DefaultLogger(DefaultLogger &&lg):_ctx(std::move(lg._ctx)),_ident(lg._ident) {}

protected:
    std::shared_ptr<Content> _ctx;
    std::string _buffer;
    std::size_t _ident = 0;
    std::size_t _counter = 0;
    std::chrono::system_clock::time_point _start_time;
};


}

}



#endif /* SRC_COROSERVER_HTTP_SERVER_H_ */
