#include "iocp.hpp"
#include "context_common.hpp"
#include <algorithm>
#include <WinSock2.h>

namespace coroserver {

    class ContextImpl;

    class TimerHandleData: public AbstractHandleData {
    public:

        TimerHandleData():AbstractHandleData(HandleType::timer) {}
    
        coro::prepared_coro sleep_until(std::chrono::system_clock::time_point tp, coro::awaitable<bool>::result p);
        coro::prepared_coro on_complete(DWORD , LPOVERLAPPED) {return {};}
        coro::prepared_coro on_timeout(std::chrono::system_clock::time_point tp);
        coro::prepared_coro on_shutdown();
        coro::prepared_coro on_error(DWORD, LPOVERLAPPED) {return {};}
        bool safe_to_close() const {return !_p;}
        void set_closing() {}
        std::chrono::system_clock::time_point get_timeout() const {return _tp;}
    protected:
        coro::awaitable<bool>::result _p;
        std::chrono::system_clock::time_point _tp = std::chrono::system_clock::time_point::max();
    };
        
    class SocketHandleData: public AbstractHandleData {
    public:
        SocketHandleData(HandleType type, SOCKET socket):AbstractHandleData(type),_socket(socket) {}
    
        SOCKET get_socket() const {return _socket;}
    
        void set_closing() {_closing = true;}

    protected:
        SOCKET _socket;
        bool _closing = false;
        std::chrono::system_clock::time_point _tp = std::chrono::system_clock::time_point::max();

    };
        
    class ServerHandleData: public SocketHandleData {
    public:

        ServerHandleData(SOCKET s, int af, ContextImpl *ctx);
        virtual ~ServerHandleData();
    
        coro::prepared_coro do_accept_async(std::chrono::system_clock::time_point tp, coro::awaitable<Context::Handle>::result p);
        coro::prepared_coro on_complete(DWORD , LPOVERLAPPED);
        coro::prepared_coro on_error(DWORD error, LPOVERLAPPED ovr);
        coro::prepared_coro on_timeout(std::chrono::system_clock::time_point tp);
        coro::prepared_coro on_shutdown();
    
        std::chrono::system_clock::time_point get_timeout() const {return _tp;}

        bool safe_to_close() const;
    
    protected:
        //buffer to store accept data
        char _accept_buffer[128];
        //overlapped buffer
        OVERLAPPED _ovr = {};
        //selected family
        int _af;
        //awaitable result
        coro::awaitable<Context::Handle>::result _p = {};
        //prepared socket for accept
        SOCKET _prepared_socket = INVALID_SOCKET;
        ContextImpl *_ctx;
    };
    
    
    enum class StreamType {
        socket,
        pipe
    };
    
    class StreamHandleTag {};
    
    class StreamHandleData: public SocketHandleData, public StreamHandleTag {
    public:

    
        StreamHandleData(SOCKET h);
        virtual ~StreamHandleData();
    
        void set_recv_buffer(char *buffer, std::size_t sz);
        void set_send_buffer(const char *buffer, std::size_t sz);
        coro::prepared_coro recv_async(std::chrono::system_clock::time_point tp, coro::awaitable<std::size_t>::result p);
        coro::prepared_coro send_async(std::chrono::system_clock::time_point tp, coro::awaitable<bool>::result p);
    
        coro::prepared_coro on_complete(DWORD , LPOVERLAPPED);
        coro::prepared_coro on_timeout(std::chrono::system_clock::time_point tp);
        coro::prepared_coro on_shutdown();
        coro::prepared_coro on_error(DWORD error, LPOVERLAPPED);
        StreamState get_state() const;
        bool safe_to_close() const;
    
        void send_close();
        void mark_opening() {_opening = true;}
        std::chrono::system_clock::time_point get_timeout() const;

        LPOVERLAPPED get_connect_overlapped() {return &_send_ovr;};

    protected:
        std::chrono::system_clock::time_point _recv_timeout = std::chrono::system_clock::time_point::max();
        std::chrono::system_clock::time_point _send_timeout = std::chrono::system_clock::time_point::max();
    
    
        char *_recv_buffer = 0;
        std::size_t _recv_buffer_size = 0;
        coro::awaitable<std::size_t>::result _recv_result = {};
        OVERLAPPED _recv_ovr = {};
    
        const char *_send_buffer = 0;
        std::size_t _send_buffer_size = 0;
        coro::awaitable<bool>::result _send_result = {};
        OVERLAPPED _send_ovr = {};

        DWORD _connect_error = 0;
        bool _state_eof = false;
        bool _send_closed = false;
        bool _opening = false;
    };

    
class TwoPipesStreamData: public AbstractHandleData, public StreamHandleTag {
public:

    TwoPipesStreamData(HANDLE in_fd, HANDLE out_fd, HANDLE process_mon, HANDLE hProcess);
    ~TwoPipesStreamData();

    std::optional<int> get_pid_status_sync();

    void set_recv_buffer(char *buffer, std::size_t sz);
    void set_send_buffer(const char *buffer, std::size_t sz);
    coro::prepared_coro recv_async(std::chrono::system_clock::time_point tp, coro::awaitable<std::size_t>::result p);
    coro::prepared_coro send_async(std::chrono::system_clock::time_point tp, coro::awaitable<bool>::result p);

    coro::prepared_coro get_pid_status_async(std::chrono::system_clock::time_point tp, coro::awaitable<int>::result p);

    coro::prepared_coro on_complete(DWORD , LPOVERLAPPED);
    coro::prepared_coro on_timeout(std::chrono::system_clock::time_point tp);
    coro::prepared_coro on_shutdown();
    coro::prepared_coro on_error(DWORD error, LPOVERLAPPED);
    void set_closing();
    StreamState get_state() const;
    bool safe_to_close() const;

    void send_close();
    std::chrono::system_clock::time_point get_timeout() const;

    ///called when handle was modified because failed iocp
    void replace_in(HANDLE newIn) {_in_fd = newIn;}
    ///called when handle was modified because failed iocp
    void replace_out(HANDLE newOut) {_out_fd = newOut;}
    

    bool terminate_process();

protected:
    std::chrono::system_clock::time_point _recv_timeout = std::chrono::system_clock::time_point::max();
    std::chrono::system_clock::time_point _send_timeout = std::chrono::system_clock::time_point::max();
    std::chrono::system_clock::time_point _pidstat_timeout =std::chrono::system_clock::time_point::max();


    HANDLE _in_fd;
    HANDLE _out_fd;
    HANDLE _process_mon;
    HANDLE _hprocess;

    char *_recv_buffer = 0;
    std::size_t _recv_buffer_size = 0;
    coro::awaitable<std::size_t>::result _recv_result = {};
    OVERLAPPED _recv_ovr = {};

    const char *_send_buffer = 0;
    std::size_t _send_buffer_size = 0;
    coro::awaitable<bool>::result _send_result = {};
    OVERLAPPED _send_ovr = {};
    
    coro::awaitable<int>::result _exit_result = {};
    OVERLAPPED _mon_ovr = {};


    DWORD _connect_error = 0;
    char _mon_buff[1];
    bool _state_eof = false;
    bool _send_closed = false;
    bool _closing = false;
};
    
class SigHandleData: public AbstractHandleData {
public:
    SigHandleData():AbstractHandleData(HandleType::signalfd) {}

    coro::prepared_coro on_complete(DWORD , LPOVERLAPPED) {return {};}
    coro::prepared_coro on_timeout(std::chrono::system_clock::time_point) {return {};}
    coro::prepared_coro on_shutdown() {return {};}
    coro::prepared_coro on_error(DWORD , LPOVERLAPPED) {return {};}
    constexpr std::chrono::system_clock::time_point get_timeout() const {return std::chrono::system_clock::time_point::max();}
    void set_closing() {}
    bool safe_to_close() const {return true;}
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

    Handle connect_process(const std::filesystem::path &path, std::span<const std::string_view> argv,  const Environment & envp);
    Handle connect_stdinout();
    bool terminate_process(Handle h);
    coro::awaitable<int> get_process_exit_status(Handle h, std::chrono::system_clock::time_point tp);
    coro::awaitable<ExitSignalType> wait_for_exit_signal();


    void thread_entry_point();
    void signal_stop();

 
protected:
    using HandleMap = HandleHashMap<PHandleData>;


    HandleMap _handleMap;
    IOCP _iocp;

    std::chrono::system_clock::time_point _awaiting_tp = std::chrono::system_clock::time_point::max();
    std::chrono::system_clock::time_point _new_tp = std::chrono::system_clock::time_point::max();
    std::size_t _awaiting_thread = 0;
    std::size_t _thread_counter = 0;
    bool _stop_signaled = false;

    mutable std::mutex _mx;

    void update_epoll_flags(Handle h, const SocketHandleData &pb);
    void update_timeout(const AbstractHandleData &hd);
    Handle create_stream(SOCKET socket);
    Handle connect_fifo(const char *fname, int flags);

    friend class ServerHandleData;
};        
        
}