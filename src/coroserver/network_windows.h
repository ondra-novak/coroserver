#include "network.h"
#include <condition_variable>
#include <memory>
#include <memory_resource>
#include <thread>
#include <set>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <WinSock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>
#include <source_location>

namespace coroserver {


class NetContextWin: public INetContext, public std::enable_shared_from_this<NetContextWin> {
public:

    explicit NetContextWin(ErrorCallback ecb);
    NetContextWin();
    ~NetContextWin();
    NetContextWin(const NetContextWin &) = delete;
    NetContextWin &operator=(const NetContextWin &) = delete;

    virtual ConnHandle connect(std::string address) override;
    virtual void reconnect(ConnHandle ident, std::string address_port) override;
    virtual void receive(ConnHandle ident, std::span<char> buffer, IPeer *peer) override;
    virtual SendStatus send(ConnHandle connection, std::string_view data) override;
    virtual void ready_to_send(ConnHandle ident, IPeer *peer) override;
    virtual ConnHandle create_server(std::string address_port) override;
    virtual void accept(ConnHandle ident, IServer *server) override;
    virtual void destroy(ConnHandle ident) override;
    std::jthread run_thread();
    void run(std::stop_token tkn);
    virtual void set_timeout(ConnHandle ident, std::chrono::system_clock::time_point tp, IPeerServerCommon *p) override;
    virtual void clear_timeout(ConnHandle ident) override;
    virtual void enqueue(SimpleAction fn) override;
    virtual ConnHandle connect(SpecialConnection type, const void *arg = nullptr) override;
    virtual PipePair create_pipe() override;
    virtual bool in_calback() const override;

protected:

    static constexpr ULONG_PTR key_offset = 2;
    static constexpr ULONG_PTR key_wakeup = 0;
    static constexpr ULONG_PTR key_exit = 1;

    using TimeoutInfo = std::pair<std::chrono::system_clock::time_point, ConnHandle>;
    using TimeoutSet = std::set<TimeoutInfo,  std::less<TimeoutInfo>,  std::pmr::polymorphic_allocator<TimeoutInfo> >;

    enum class SocketType : char {
        unknown,
        peer,
        server
    };

    struct SocketInfoCommon {
        ConnHandle _ident = invalid_connect_handle;    //this connection handle
        union {
            SOCKET _socket = INVALID_SOCKET;                    //associated socket
            HANDLE _pipe_handle;
        };
        bool _is_handle = false;
        bool _error = false;                                //error reported and connection is lost        
        SocketType _type =  SocketType::unknown;
        int _cb_call_cntr = {};                                //count of currently active callbacks (must be 0 to destroy)
        IPeerServerCommon *_timeout_cb = {};                //callback object for timeout        
        std::chrono::system_clock::time_point _tmtp = {};   //current scheduled timeout - function set_timeout()
    };

    struct SocketInfo: SocketInfoCommon  {
        std::span<char> _recv_buffer;                       //reference to receiving buffer
        std::string_view _to_send_data = {};                //pending send data (view)
        OVERLAPPED _send_ovr = {};                              //OVERLAPPED for send or connect
        OVERLAPPED _recv_ovr = {};                              //OVERLAPPED for recv or accept
        IPeer *_recv_cb = {};                               //callback object for recv
        IPeer *_send_cb = {};                               //callback object for send
        bool _destroy_on_cancel_read = false;
        bool _destroy_on_cancel_write = false;
        bool _clear_to_send = false;                        //sending is allowed
        bool _connecting = false;                            //socket is connecting (connect)
    };

    struct SocketAcceptInfo : SocketInfoCommon {
        char buff[128];
        SOCKET _accept_socket = INVALID_SOCKET;
        int _af;                                            //AF socket family of current socket (need for accept)
        IServer *_accept_cb = {};                           //callback object for accept
        OVERLAPPED _ovr = {};                              //OVERLAPPED for recv or accept
    };

    static_assert(std::is_trivially_destructible_v<SocketInfo>);
    static_assert(std::is_trivially_destructible_v<SocketAcceptInfo>);

    using SocketList = std::vector<std::unique_ptr<SocketInfoCommon> >;


    mutable std::mutex _mx;
    ErrorCallback _ecb;
    HANDLE _completion_port;
    SocketList _sockets;
    std::pmr::unsynchronized_pool_resource _pool;
    TimeoutSet _tmset;
    std::condition_variable _cond;
    bool _need_timeout_thread = false;
    std::vector<SimpleAction> _actions;
    ConnHandle _next_handle = 1;


    template<typename Type>
    Type *alloc_socket_lk();
    void rehash();
    void free_socket_lk(ConnHandle id);
    SocketInfoCommon *socket_by_ident(ConnHandle id);
    SOCKET connect_peer(std::string address_port, DWORD key, OVERLAPPED *ovr);
    void run_worker(std::stop_token tkn) ;
    DWORD get_completion_timeout_lk();
    std::chrono::system_clock::time_point get_completion_timeout_tp_lk();
    void process_event_lk(std::unique_lock<std::mutex> &lk, ConnHandle h,  DWORD transfered, OVERLAPPED *ovr, DWORD error);
    template<typename E> void report_error(E exception, std::string_view action, std::source_location loc = std::source_location::current());
    void report_last_error(std::string_view action, std::source_location loc = std::source_location::current());

    template<typename Fn>
    void invoke_cb_lk(std::unique_lock<std::mutex> &lk, ConnHandle id, Fn &&fn);
    SocketInfoCommon *wait_for_finish_cbs_lk(std::unique_lock<std::mutex> &lk, ConnHandle id);
};

class NetThreadedContext: public NetContextWin {
public:

    NetThreadedContext(ErrorCallback ecb, int threads);
    ~NetThreadedContext();
    void start();

protected:
    std::vector<std::jthread> _threads;
};




class Win32ErrorCategory: public std::error_category {
public:
    virtual ~Win32ErrorCategory() noexcept = default;
    virtual const char* name() const noexcept override;
    virtual std::string message(int _Errval) const override;
};


class Win32Error: public std::system_error {
public:
    Win32Error();
    Win32Error(std::string message);
    Win32Error(DWORD error);
    Win32Error(DWORD error, std::string message);
};

}
