#include <algorithm>
#include <chrono>
#include "context.hpp"
#include "epollpp.hpp"
#include "handle_hash_map.hpp"
#include "eventfd.hpp"

namespace coroserver {

enum class HandleType {
    timer,
    server,
    socket,
    pipes
};

struct TwoCoros {
    coro::prepared_coro a = {};
    coro::prepared_coro b = {};
    TwoCoros() = default;
    TwoCoros(coro::prepared_coro a):a(std::move(a)) {}
    TwoCoros(coro::prepared_coro a, coro::prepared_coro b)
        :a(std::move(a)), b(std::move(b)) {}
};

class AbstractHandleData {
public:

    AbstractHandleData(HandleType type):_type(type) {}

    HandleType get_type() const {return _type;}


    template<typename Fn>
    auto visit(Fn &&fn);
    template<typename Fn>
    auto visit(Fn &&fn) const;


    StreamState get_state() const {return _was_shutdown?StreamState::closed:StreamState::active;}
protected:
    HandleType _type;
    bool _was_shutdown = false;
};

class TimerHandleData: public AbstractHandleData {
public:

    TimerHandleData():AbstractHandleData(HandleType::timer) {}

    coro::prepared_coro sleep_until(std::chrono::system_clock::time_point tp, coro::awaitable<bool>::result p);
    coro::prepared_coro on_complete(int) {return {};}
    coro::prepared_coro on_timeout(std::chrono::system_clock::time_point tp);
    coro::prepared_coro on_shutdown();

    template<std::invocable<int, int> Svc>
    void apply_epoll_flags(Svc &&) const {}

    const std::chrono::system_clock::time_point& get_timeout() const {return _tp;}

protected:
    coro::awaitable<bool>::result _p;
    std::chrono::system_clock::time_point _tp = std::chrono::system_clock::time_point::max();
};

class SocketHandleData: public AbstractHandleData {
public:
    SocketHandleData(HandleType type, int socket):AbstractHandleData(type),_socket(socket) {}

//    int get_socket() const {return _socket;}

    std::string get_host() const;

    ~SocketHandleData();

protected:
    int _socket;

};


class ServerHandleData: public SocketHandleData {
public:

    ServerHandleData(int socket, ContextImpl *ctx);

    int do_accept_sync();
    coro::prepared_coro do_accept_async(std::chrono::system_clock::time_point tp, coro::awaitable<Context::Handle>::result p);
    coro::prepared_coro on_complete(int flags);
    coro::prepared_coro on_timeout(std::chrono::system_clock::time_point tp);
    coro::prepared_coro on_shutdown();

    template<std::invocable<int, int> Svc>
    void apply_epoll_flags(Svc &&svc) const;

    const std::chrono::system_clock::time_point& get_timeout() const {return _tp;}


protected:
    coro::awaitable<Context::Handle>::result _p = {};
    std::chrono::system_clock::time_point _tp = std::chrono::system_clock::time_point::max();
    ContextImpl *_ctx;
};


class StreamHandleTag {};

class StreamHandleData: public SocketHandleData, public StreamHandleTag {
public:

    StreamHandleData(int fd);

    int recv_sync(char *buffer, std::size_t sz);
    int send_sync(const char *buffer, std::size_t sz);

    ///receive async - must be used after recv_sync to set buffers
    coro::prepared_coro recv_async(std::chrono::system_clock::time_point tp, coro::awaitable<std::size_t>::result p);
    ///send async - must be used after send_sync to set buffers
    coro::prepared_coro send_async(std::chrono::system_clock::time_point tp, coro::awaitable<bool>::result p);

    TwoCoros on_complete(int flags);
    TwoCoros on_timeout(std::chrono::system_clock::time_point tp);
    TwoCoros on_shutdown();
    StreamState get_state() const;

    void send_close();

    template<std::invocable<int, int> Svc>
    void apply_epoll_flags(Svc &&svc) const;

    std::chrono::system_clock::time_point get_timeout() const;


protected:
    std::chrono::system_clock::time_point _recv_timeout = std::chrono::system_clock::time_point::max();
    std::chrono::system_clock::time_point _send_timeout = std::chrono::system_clock::time_point::max();

    int do_recv();
    int do_send();

    char *_recv_buffer = 0;
    std::size_t _recv_buffer_size = 0;
    coro::awaitable<std::size_t>::result _recv_result = {};

    const char *_send_buffer = 0;
    std::size_t _send_buffer_size = 0;
    coro::awaitable<bool>::result _send_result = {};
    int _connect_error = 0;
    bool _state_eof = false;
    bool _send_closed = false;
    bool _opening = true;


};


class ContextImpl {
public:

    ContextImpl();
    using Handle = Context::Handle;
    static constexpr auto null_handle = Context::null_handle;

    Handle create_server(std::string host, std::string def_port);
    Handle connect(std::string host, std::string def_port);
    Handle connect(SpecialDevice dev);
    Handle create_timer();
    void close(Handle h);
    std::string get_host(Handle h) const;
    coro::awaitable<bool> sleep(Handle timer, std::chrono::system_clock::time_point tp);
    coro::awaitable<Handle> accept(Handle server, std::chrono::system_clock::time_point timeout);
    coro::awaitable<size_t> receive(Handle stream, char *buffer, std::size_t sz, std::chrono::system_clock::time_point timeout);
    coro::awaitable<bool> send(Handle stream, const char *buffer, std::size_t sz, std::chrono::system_clock::time_point timeout);
    coro::awaitable<bool> send_eof(Handle stream);
    StreamState get_state(Handle h);
    void shutdown(Handle h);

    void thread_entry_point();
    void signal_stop();

protected:

    struct HandleDataDeleter {
        void operator()(AbstractHandleData *p);
    };

    using PHandleData = std::unique_ptr<AbstractHandleData, HandleDataDeleter>;
    using HandleMap = HandleHashMap<PHandleData>;


    HandleMap _handleMap;
    EPoll<Handle> _epoll;
    EventFd _epoll_wk;

    std::chrono::system_clock::time_point _awaiting_tp = std::chrono::system_clock::time_point::max();
    std::chrono::system_clock::time_point _new_tp = std::chrono::system_clock::time_point::max();
    std::size_t _awaiting_thread = 0;
    std::size_t _thread_counter = 0;
    bool _stop_signaled = false;

    mutable std::mutex _mx;

    void update_epoll_flags(Handle h, const AbstractHandleData &pb);
    void update_timeout(const AbstractHandleData &hd);
    Handle create_stream(int socket);
    Handle connect_fifo(const char *fname, int flags);

    friend class ServerHandleData;


};


}


