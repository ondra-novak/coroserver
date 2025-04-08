#include <algorithm>
#include <chrono>
#include <map>
#include "context_common.hpp"
#include "epollpp.hpp"
#include "eventfd.hpp"

namespace coroserver {


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

class TwoPipesStreamData: public AbstractHandleData, public StreamHandleTag {
public:

    TwoPipesStreamData(int in_fd, int out_fd, pid_t pid);
    ~TwoPipesStreamData();

    int recv_sync(char *buffer, std::size_t sz);
    int send_sync(const char *buffer, std::size_t sz);
    std::optional<int> get_pid_status_sync();

    ///receive async - must be used after recv_sync to set buffers
    coro::prepared_coro recv_async(std::chrono::system_clock::time_point tp, coro::awaitable<std::size_t>::result p);
    ///send async - must be used after send_sync to set buffers
    coro::prepared_coro send_async(std::chrono::system_clock::time_point tp, coro::awaitable<bool>::result p);

    coro::prepared_coro get_pid_status_async(std::chrono::system_clock::time_point tp, coro::awaitable<int>::result p);

    TwoCoros on_complete(int flags);
    TwoCoros on_timeout(std::chrono::system_clock::time_point tp);
    TwoCoros on_shutdown();
    StreamState get_state() const;

    void send_close();

    template<std::invocable<int, int> Svc>
    void apply_epoll_flags(Svc &&svc) const;

    std::chrono::system_clock::time_point get_timeout() const;

    coro::prepared_coro on_status_available(int status);

    bool terminate_process();


protected:
    std::chrono::system_clock::time_point _recv_timeout = std::chrono::system_clock::time_point::max();
    std::chrono::system_clock::time_point _send_timeout = std::chrono::system_clock::time_point::max();
    std::chrono::system_clock::time_point _pidstat_timeout =std::chrono::system_clock::time_point::max();

    int do_recv();
    int do_send();

    int _in_fd;
    int _out_fd;
    int _pid;

    char *_recv_buffer = 0;
    std::size_t _recv_buffer_size = 0;
    coro::awaitable<std::size_t>::result _recv_result = {};

    const char *_send_buffer = 0;
    std::size_t _send_buffer_size = 0;
    coro::awaitable<bool>::result _send_result = {};

    std::optional<int> _process_status;
    coro::awaitable<int>::result _process_status_promise = {};

    bool _state_eof = false;
    bool _send_closed = false;
};



class SigHandleData: public AbstractHandleData {
public:
    enum SigType {
        schild,
        sbreak
    };

    SigHandleData(SigType sigtype);

    TwoCoros on_complete(int flags);
    TwoCoros on_timeout(std::chrono::system_clock::time_point)  {return {};}
    TwoCoros on_shutdown() {return {};}
    StreamState get_state() const {return {};}


    template<std::invocable<int, int> Svc>
    void apply_epoll_flags(Svc &&) const {}

    std::chrono::system_clock::time_point get_timeout() const {
        return std::chrono::system_clock::time_point::max();
    }
protected:
    SigType _sigtype;

};


class ContextImpl {
public:

    ContextImpl();
    using Handle = Context::Handle;
    static constexpr auto null_handle = Context::null_handle;

    Handle create_server(std::string host, std::string def_port);
    Handle connect(std::string host, std::string def_port);

    Handle connect_process(std::filesystem::path path, std::span<const std::string_view> argv,  const Environment & envp);
    Handle connect_stdinout();
    bool terminate_process(Handle h);
    coro::awaitable<int> get_process_exit_status(Handle h, std::chrono::system_clock::time_point tp);

    Handle create_from_handles(int rd_fd, int wr_fd, pid_t pid);

    coro::awaitable<ExitSignalType> wait_for_exit_signal();

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


    using HandleMap = HandleHashMap<PHandleData>;

    HandleMap _handleMap;
    EPoll<Handle> _epoll;
    EventFd _epoll_wk;
    Handle _child_monitor = null_handle;
    Handle _break_monitor = null_handle;

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

