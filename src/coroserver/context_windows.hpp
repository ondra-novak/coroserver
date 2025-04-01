#include "iocp.hpp"
#include "context.hpp"
#include "handle_hash_map.hpp"
#include <algorithm>
#include <WinSock2.h>

namespace coroserver {

    class ContextImpl;

    
    class AbstractHandleData {
    public:
        virtual ~AbstractHandleData() = default;
    
    
        template<typename Fn>
        auto visit(Fn &&fn);
    
        template<typename X>
        bool is_base_of() const;
    
        const std::chrono::system_clock::time_point& get_timeout() const {return _tp;}
        StreamState get_state() const {return _shutted_down?StreamState::closed:StreamState::active;}
    protected:
        std::chrono::system_clock::time_point _tp = std::chrono::system_clock::time_point::max();
        bool _shutted_down = false;
    };
    
    class TimerHandleData: public AbstractHandleData {
    public:
    
        coro::prepared_coro sleep_until(std::chrono::system_clock::time_point tp, coro::awaitable<bool>::result p);
        coro::prepared_coro on_complete(DWORD , LPOVERLAPPED) {return {};}
        coro::prepared_coro on_timeout(std::chrono::system_clock::time_point tp);
        coro::prepared_coro on_shutdown();
        coro::prepared_coro on_error(DWORD, LPOVERLAPPED) {return {};}
        bool safe_to_close() const {return !_p;}
        void set_closing() {}
        protected:
        coro::awaitable<bool>::result _p;
    };
        
    class SocketHandleData: public AbstractHandleData {
    public:
        SocketHandleData(SOCKET socket):_socket(socket) {}
        SocketHandleData(HANDLE handle):_handle(handle) {}
    
        SOCKET get_socket() const {return _socket;}
        HANDLE get_handle() const {return _handle;}        
    
        void set_closing() {_closing = true;}

    protected:
        union {
            SOCKET _socket;
            HANDLE _handle;
        };
        bool _closing;

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
    
    template<StreamType type>
    class StreamHandleData: public SocketHandleData, public StreamHandleTag {
    public:

        using H = std::conditional_t<type == StreamType::socket, SOCKET, HANDLE>;
    
        StreamHandleData(H h);
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
    
    
        void update_timeout();
    };
    
    
    template<typename Fn>
    auto AbstractHandleData::visit(Fn &&fn) {
        const std::type_info &t = typeid(*this);
        if (t == typeid(StreamHandleData<StreamType::socket>)) {
            return fn(*static_cast<StreamHandleData<StreamType::socket> *>(this));
        } else if (t == typeid(StreamHandleData<StreamType::pipe>)) {
            return fn(*static_cast<StreamHandleData<StreamType::pipe> *>(this));
        } else if (t == typeid(ServerHandleData)) {
            return fn(*static_cast<ServerHandleData *>(this));
        } else if (t == typeid(TimerHandleData)) {
            return fn(*static_cast<TimerHandleData *>(this));
        } else {
            throw std::logic_error("unknown handle data");
        }
    }
    
    
    template<typename X>
    bool AbstractHandleData::is_base_of() const {
        return visit([](const auto &x){
            using T = std::decay_t<decltype(x)>;
            return std::is_base_of_v<X, T>;
        });
    }
    
    

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
    using PHandleData = std::unique_ptr<AbstractHandleData>;
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