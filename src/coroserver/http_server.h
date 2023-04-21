#pragma once
#ifndef SRC_COROSERVER_HTTP_SERVER_H_
#define SRC_COROSERVER_HTTP_SERVER_H_

#include "http_server_request.h"

#include <cocls/function.h>

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
    Handler() = default;

    template<typename Fn>
    CXX20_REQUIRES(std::invocable<Fn, ServerRequest &, std::string_view>)
    Handler(Fn &&fn) {
        class Impl: public IHandler {
        public:
            Impl(Fn &&fn):_fn(std::forward<Fn>(fn)) {}
            virtual cocls::future<void> call(ServerRequest &req, std::string_view vpath) const noexcept {
                try {
                    return _fn(req, vpath);
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
                try {
                    return _fn(req);
                } catch (...) {
                    return cocls::future<void>::set_exception(std::current_exception());
                }
            }
        protected:
            Fn _fn;
        };
        _ptr = std::make_shared<Impl>(std::forward<Fn>(fn));
    }

    cocls::future<void> call(ServerRequest &req, std::string_view vpath) {
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

#if 0

class MappedHandler {
public:
    MappedHandler(Handler handler, std::string_view vpath)
        :_handler(handler)
        ,vpath(vpath) {}
    cocls::future<void> operator()(ServerRequest &req) {
        return _handler(req, vpath);
    }
protected:
    Handler _handler;
    std::string_view vpath;
};

template<typename Mapper>
class Server {
public:
    Server(const Mapper &mapper):_mapper(mapper) {}


    template<typename Tracer>
    CXX20_REQUIRES(std::invocable<Tracer, TraceEvent, ServerRequest &>)
    cocls::future<void> serve(cocls::generator<Stream> tcp_server, Tracer tracer) {
        return [&](cocls::promise<void> prom) {
            _exit_promise = std::move(prom);
            serve_gen(std::move(tcp_server), std::move(tracer)).detach();
        };
    }



protected:
    const Mapper &_mapper;
    cocls::promise<void> _exit_promise;
    std::atomic<int> _requests = 0;

    void exit_request() {
        if ((--_requests) == 0) _exit_promise();
    }

    template<typename Tracer>
    cocls::async<void> serve_gen(cocls::generator<Stream> tcp_server, Tracer tracer) {
        ++_requests;
        while (co_await tcp_server.next()) {
            ++_requests;
            serve_req(std::move(tcp_server.value()), tracer);
        }
        exit_request();
    }



    template<typename Tracer>
    cocls::async<void> serve_req(Stream s, Tracer tracer) {
        try {
            ServerRequest req(s);
            tracer(TraceEvent::open, req);
            std::exception_ptr e;
            while (req.load()) {
                try {
                    tracer(TraceEvent::open, req);
                    auto srch = _mapper(req);
                    cocls::future<void> res;
                    while (srch) {
                        MappedHandler = srch();

                    }
                    do {

                    }
                    MappedHandler handler = _mapper(req);
                    co_await handler(req);
                    if (req)



                } catch (...) {
                    e = std::current_exception();
                }
            }
            exit_request();
        } catch (...) {
            exit_request();
            //can't report this exception
        }
    }

};

#endif

}

}



#endif /* SRC_COROSERVER_HTTP_SERVER_H_ */
