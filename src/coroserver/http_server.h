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
    virtual cocls::future<void> call(ServerRequest &req, std::string_view vpath) const noexcept= 0;
    virtual ~IHandler() = default;
};

class Handler {
public:
    constexpr Handler() = default;

    template<typename Fn>
    CXX20_REQUIRES(std::invocable<Fn, ServerRequest &, std::string_view>)
    Handler(Fn &&fn) {
        class Impl: public IHandler {
        public:
            Impl(Fn &&fn):_fn(std::forward<Fn>(fn)) {}
            virtual cocls::future<void> call(ServerRequest &req, std::string_view vpath) const noexcept {
                using RetVal = decltype(_fn(req, vpath));
                try {
                    if constexpr(std::is_void_v<RetVal>) {
                        _fn(req, vpath);
                        return cocls::future<void>::set_value();
                    } else {
                        return _fn(req, vpath);
                    }
                } catch (...) {
                    return cocls::future<void>::set_exception(std::current_exception());
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
            virtual cocls::future<void> call(ServerRequest &req, std::string_view ) const noexcept {
                using RetVal = decltype(_fn(req));
                try {
                    if constexpr(std::is_void_v<RetVal>) {
                        _fn(req);
                        return cocls::future<void>::set_value();
                    } else {
                        return _fn(req);
                    }
                } catch (...) {
                    return cocls::future<void>::set_exception(std::current_exception());
                }
            }
        protected:
            Fn _fn;
        };
        _ptr = std::make_shared<Impl>(std::forward<Fn>(fn));
    }

    operator bool() const {return _ptr != nullptr;}

    cocls::future<void> call(ServerRequest &req, std::string_view vpath) const {
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
                res.append(strMethod(static_cast<Method>(i)));
                ++i;
                while(i < static_cast<int>(Method::unknown)) {
                    if (bitvector & (1<<i)) {
                        res.append(", ");
                        res.append(strMethod(static_cast<Method>(i)));
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

class Server {
public:

    static std::string_view error_handler_prefix;

    template<typename Tracer>
    CXX20_REQUIRES(std::invocable<Tracer, TraceEvent, ServerRequest &>)
    cocls::future<void> serve(cocls::generator<Stream> tcp_server, Tracer tracer) {
        return [&](cocls::promise<void> prom) {
            _exit_promise = std::move(prom);
            serve_gen(std::move(tcp_server), std::move(tracer)).detach();
        };
    }

    cocls::future<void> serve(cocls::generator<Stream> tcp_server) {
        return serve(std::move(tcp_server),[](TraceEvent, ServerRequest &) {});
    }

    template<typename ContextIO, typename Tracer>
    cocls::future<void> run(ContextIO ctx, std::vector<PeerName> ports, Tracer tracer) {
        return serve(ctx.accept(), tracer);
    }

    template<typename ContextIO>
    cocls::future<void> run(ContextIO ctx, std::vector<PeerName> ports) {
        return serve(ctx.accept(),[](TraceEvent, ServerRequest &) {});
    }

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

    cocls::future<void> serve_req(Stream s) {
        return serve_req_coro(std::move(s), [](TraceEvent, ServerRequest &) {});
    }
    template<typename Tracer>
    cocls::future<void> serve_req(Stream s, Tracer tracer) {
        return serve_req_coro(std::move(s), std::move(tracer));
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
            cocls::future<void> fut;
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

    cocls::future<void> send_error_page(ServerRequest &req);
    void select_handler(ServerRequest &req, cocls::future<void> &fut);
};

template<typename Output>
class DefaultLogger {

    struct Content {
        std::mutex _mx;
        Output _output;
        Content (Output &&out):_output(out) {}
        void send_out(std::string_view text) {
            std::lock_guard _(_mx);
            _output(text);
        }
    };



public:
    DefaultLogger(Output output):_ctx(std::make_shared<Content>(std::forward<Output>(output))) {}
    DefaultLogger(const DefaultLogger &lg):_ctx(lg._ctx) {}

protected:
    std::shared_ptr<Content> _ctx;
    std::string _buffer;

};


}

}



#endif /* SRC_COROSERVER_HTTP_SERVER_H_ */
