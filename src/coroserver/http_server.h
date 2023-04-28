#pragma once
#ifndef SRC_COROSERVER_HTTP_SERVER_H_
#define SRC_COROSERVER_HTTP_SERVER_H_

#include "http_server_request.h"
#include "prefixmap.h"

#include <cocls/function.h>
#include <cocls/generator.h>
#include <shared_mutex>
#include <memory>



namespace coroserver {

namespace http {

class IHandler {
public:

    class Ret: public cocls::future<void> {
    public:
        using cocls::future<void>::future;
        template<typename Fn>
        CXX20_REQUIRES(((std::is_integral_v<typename decltype(std::declval<Fn>()())::value_type>
                        && sizeof(typename decltype(std::declval<Fn>()())::value_type) <= sizeof(void *))
                        || std::is_void_v<typename decltype(std::declval<Fn>()())::value_type>))
        Ret(Fn &&fn) {
            new(this) auto(fn());
        }
        template<typename Fn>
        CXX20_REQUIRES(((std::is_integral_v<typename decltype(std::declval<Fn>()())::value_type>
                        && sizeof(typename decltype(std::declval<Fn>()())::value_type) <= sizeof(void *))
                        || std::is_void_v<typename decltype(std::declval<Fn>()())::value_type>))
        void operator<<(Fn &&fn) {
            this->~Ret();
            try {
                new(this) auto(fn());
            } catch (...) {
                new(this) auto([]()->cocls::future<void>{return cocls::future<void>::set_exception(std::current_exception());});
            }
        }
    };

    virtual Ret call(ServerRequest &req, std::string_view vpath) const noexcept= 0;
    virtual ~IHandler() = default;
};

///Defines of http handler
/**
 * The http handler can be any lambda function, which acceptes one or two arguments.
 * It always accepts ServerRequest & as first argument and optionally can accept std::string_view
 * vpath as second argument. In this case, vpath contains relative path of request to
 * handler's path.
 *
 * The function should return one of three variants of return value. It can return void, in
 * case, that request was handled synchronously, it can return future<void> in case
 * that request can be processed asynchronously, or it can return future<bool> for the
 * same reason, however the value is ignored. This allows to return directly result of
 * req.send() operation, especially when it is last operation of the handler (so the
 * handler don't need to be coroutine)
 */
class Handler {
public:
    constexpr Handler() = default;

    template<typename Fn>
    CXX20_REQUIRES(std::invocable<Fn, ServerRequest &, std::string_view>)
    Handler(Fn &&fn) {
        class Impl: public IHandler {
        public:
            Impl(Fn &&fn):_fn(std::forward<Fn>(fn)) {}
            virtual Ret call(ServerRequest &req, std::string_view vpath) const noexcept {
                using RetVal = decltype(_fn(req, vpath));
                try {
                    if constexpr(std::is_void_v<RetVal>) {
                        _fn(req, vpath);
                        return [&]{return cocls::future<void>::set_value();};
                    } else {
                        return Ret([&]{return _fn(req, vpath);});
                    }
                } catch (...) {
                    return [&]{return cocls::future<void>::set_exception(std::current_exception());};
                }
            }
        protected:
            Fn _fn;
        };
        _ptr = std::make_shared<Impl>(std::forward<Fn>(fn));
    }

    template<typename Fn>
    CXX20_REQUIRES(std::invocable<Fn, ServerRequest &>)
    Handler(Fn &&fn) {
        class Impl: public IHandler {
        public:
            Impl(Fn &&fn):_fn(std::forward<Fn>(fn)) {}
            virtual Ret call(ServerRequest &req, std::string_view ) const noexcept {
                using RetVal = decltype(_fn(req));
                try {
                    if constexpr(std::is_void_v<RetVal>) {
                        _fn(req);
                        return [&]{return cocls::future<void>::set_value();};
                    } else {
                        return Ret([&]{return _fn(req);});
                    }
                } catch (...) {
                    return [&]{return cocls::future<void>::set_exception(std::current_exception());};
                }
            }
        protected:
            Fn _fn;
        };
        _ptr = std::make_shared<Impl>(std::forward<Fn>(fn));
    }

    operator bool() const {return _ptr != nullptr;}

    IHandler::Ret call(ServerRequest &req, std::string_view vpath) const {
        return _ptr->call(req, vpath);
    }




protected:
    std::shared_ptr<const IHandler> _ptr;
};

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
    exception
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
    std::size_t call_handler(ServerRequest &req, IHandler::Ret &fut);
    ///Calls handler for given request
    /**
     * @param req request
     * @param vpath  overrides path (virtual path) - this path is used for lookup
     * @param fut reference to future to resolve once the handler is finished.
     * @retval 0 handler called and accepted the request
     * @retval 1 handler was not found (or handler rejected the request)
     * @retval >1 found handler, but for different method. The value contains bitmask of all found methods (bit 0 is always set)
     */
    std::size_t call_handler(ServerRequest &req, std::string_view vpath, IHandler::Ret &fut);
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
    std::size_t call_handler(ServerRequest &req, Method methodOverride, std::string_view vpath, IHandler::Ret &fut);

protected:
    PrefixMap<MethodMap> _endpoints;

};

class Server: protected Router {
public:

    using Router::Router;

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
    template<typename Tracer>
    CXX20_REQUIRES(std::invocable<Tracer, TraceEvent, ServerRequest &>)
    cocls::future<void> start(cocls::generator<Stream> tcp_server, Tracer tracer) {
        return [&](cocls::promise<void> prom) {
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
    cocls::future<void> start(cocls::generator<Stream> tcp_server) {
        return start(std::move(tcp_server),[](TraceEvent, ServerRequest &) {});
    }

    ///Manually serve on given connection
    /**
     * @param s stream to be handled by the server
     * @return future which is resolved once the connection is closed
     *
     */
    cocls::future<void> serve_req(Stream s) {
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
    cocls::future<void> serve_req(Stream s, Tracer tracer) {
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
    std::shared_mutex _mx;
    PrefixMap<MethodMap> _endpoints;
    cocls::promise<void> _exit_promise;
    std::atomic<int> _requests = 0;

    friend class std::lock_guard<Server>;

    void lock() {
        ++_requests;
    }

    void unlock() {
        if ((--_requests) == 0) _exit_promise();
    }


    template<typename Tracer>
    cocls::async<void> serve_gen(cocls::generator<Stream> tcp_server, Tracer tracer) {
        std::lock_guard _(*this);
        while (co_await tcp_server.next()) {
            serve_req_coro(std::move(tcp_server.value()), tracer).detach();
        }
        co_return;
    }


    template<typename Tracer>
    cocls::async<void> serve_req_coro(Stream s, Tracer tracer) {
        //lock this object - count request - this is called in context of serve()
        std::lock_guard _(*this);
        //prepare server request
        ServerRequest req(s);
        //report that request has been opened
        tracer(TraceEvent::open, req);
        //load requests from the stream - return false if error
        while (co_await req.load()) {
            //future to await handler
            IHandler::Ret fut;
            try {
                //report that request has been loaded
                tracer(TraceEvent::load, req);
                //select matching handler and call it, set future with result
                select_handler(req, fut);
                //await for future
                co_await fut;
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
            co_await send_error_page(req);
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
            co_await send_error_page(req);
            //and report finish
            tracer(TraceEvent::finish, req);
            //keep alive is impossible here
        }
        //so report close
        tracer(TraceEvent::close, req);
        //and exit
        co_return;
    }

    IHandler::Ret send_error_page(ServerRequest &req);
    void select_handler(ServerRequest &req, IHandler::Ret &fut);
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
                                     break;
            case TraceEvent::close: _buffer.append("closed. Read: ");
                                    _start_time = {};
                                    {
                                        auto cntr = req.get_counters();
                                        _buffer.append(std::to_string((cntr.read+512)/1024));
                                        _buffer.append(" KiB / Write: ");
                                        _buffer.append(std::to_string((cntr.write+512)/1024));
                                        _buffer.append(" KiB");

                                    }
                                    break;
        }
        if (_start_time != std::chrono::system_clock::time_point()) {
            _buffer.append(std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()- _start_time).count()));
            _buffer.append(" ms, ");
            _buffer.append(std::to_string((counter+512)/1024));
            _buffer.append(" KiB");
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
