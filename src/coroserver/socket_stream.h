#pragma once
#include <queue>
#include <mutex>
#include "coroutines.h"
#include "stream.h"
#include "network.h"
#include "ring_buffer.h"

namespace coroserver {


class SocketStream : public IStream, public IPeer {
public:

    virtual ~SocketStream();
    virtual awaitable<std::string_view> receive()override;
    virtual void put_back(std::string_view s) override;
    virtual awaitable<bool> send(std::string_view data) override;
    StreamState get_state() const override;
    virtual awaitable<void> close() override;
    virtual IOTimeout get_timeouts() const override;
    virtual IStream::Counters get_counters() const override;
    virtual void set_timeouts(coroserver::IOTimeout tm)  override;

    ///create socket stream
    /**
     * @param ctx network context
     * @param h connection handle
     * @return stream. The returned stream is in opening state until
     * clear to send is manifested
     */
    static Stream create(std::shared_ptr<INetContext> ctx, ConnHandle h);
    ///connect stream to address
    /**
     * @param ctx network context
     * @param address_port address:port
     * @return stream. The stream is connected asynchronously, so initially
     * it is in opening state until the connection is established. However you
     * can use it to send data, which are buffered and sent once connection
     * is established. If connection is refused, the buffered data are
     * discarded
     */
    static Stream connect(std::shared_ptr<INetContext> ctx, std::string address_port);
    static Stream connect(std::shared_ptr<INetContext> ctx, SpecialConnection type, const void *arg = nullptr);

    ///create socket server with a filter
    /**
     * @param ctx network context
     * @param address_port address:port
     * @param stp stop token, it is used to stop server
     * @param flt a function which filters connections by their peer address. Each
     * call receives peer address and must return true - accept connection or
     * false - reject connection
     * @return generator
     */
    template<std::invocable<std::string> _Filter>
    static async_generator<Stream> create_tcp_server(std::shared_ptr<INetContext> ctx, std::string address_port, std::stop_token stp, _Filter flt);
    ///create socket server
    /**
     * @param ctx network context
     * @param address_port address:port
     * @param stp stop token, it is used to stop the server
     * @return generator
     */
    static async_generator<Stream> create_tcp_server(std::shared_ptr<INetContext> ctx, std::string address_port, std::stop_token stp);

    SocketStream(const  SocketStream &) = delete;
    SocketStream &operator=(const  SocketStream &) = delete;

protected:
    SocketStream(std::shared_ptr<INetContext> ctx, ConnHandle h);

    mutable std::mutex _mx;
    std::shared_ptr<INetContext> _ctx;
    ConnHandle _h;
    IOTimeout _tms;
    std::chrono::system_clock::time_point current_recv_tm = std::chrono::system_clock::time_point::max();
    std::chrono::system_clock::time_point current_send_tm = std::chrono::system_clock::time_point::max();
    std::chrono::system_clock::time_point current_tm = {};

    std::vector<char> _input_buffer;
    std::string_view _input_buffer_ready;
    awaitable<std::string_view>::result _receive_promise;
    std::size_t _input_buffer_size = 1500;
    std::size_t _count_input_bytes = 0;
    bool _receiving = false;
    bool _is_eof = false;

    awaitable<bool>::result _awaiting_write = {};
    std::string_view _output_view = {};
    ///count of sent bytes (total)
    std::size_t _count_output_bytes = 0;
    ///If true, we can immediately send data, if false, pending send
    bool _clear_to_send = false;
    ///if true, send eof once you finish all buffers, if false nothing
    bool _send_eof = false;
    ///if true, output has been closed, nothing can be sent
    bool _output_closed = false;
    ///if true, stream is in opening state
    bool _opening_state = true;

    virtual void receive_complete(std::string_view data) noexcept override;
    virtual void on_timeout() noexcept override;
    virtual void clear_to_send() noexcept override;
    void ready_to_send();

    void destroy_me();

    void begin_receive();

    void update_timer();
};

///TCPServer as object
class TCPServer: public IServer {
public:

    struct AcceptInfo {
        ConnHandle handle;
        std::string peer_addr;
    };
    using AWT = awaitable<AcceptInfo>;
    using PROM = AWT::result;

    ///Construct server object
    /**
     * @param ctx network context
     * @param address_port address:port (use address * for all interfaces)
     */
    TCPServer(std::shared_ptr<INetContext> ctx, std::string address_port);
    ~TCPServer();

    TCPServer(const TCPServer &) = delete;
    TCPServer &operator=(const TCPServer &) = delete;

    ///awaitable accept
    awaitable<AcceptInfo> accept_handle();

    ///awaitable accept
    /**
     * @param addr_port fill variable with address and port of returned stream
     * @return
     */
    awaitable<Stream> accept(std::string &addr_port);


    ///awaitable accept
    /**
     * @return stream
     */
    awaitable<Stream> accept();

    ///cancel accept operation
    /**
     * After cancel(), any call of accept returns nullopt
     *
     * this operation also blocks object, it is no longer able to accept
     * connections, so it should be destroyed
     *
     */
    prepared_coro cancel();

protected:
    std::shared_ptr<INetContext> _ctx;
    ConnHandle _h;
    std::atomic<AWT *> _r = {};

    void do_accept_raw(awaitable<AcceptInfo> &ainfo, awaitable<Stream>::result &, std::string *&);
    await_member_callback<AcceptInfo, TCPServer *,
            &TCPServer::do_accept_raw, awaitable<Stream>::result, std::string *> _accept_cb;
    virtual void on_accept(ConnHandle connection, std::string peer_addr) noexcept override;
    virtual void on_timeout() noexcept override {}
};



template<std::invocable<std::string> _Filter>
inline async_generator<Stream> coroserver::SocketStream::create_tcp_server(
        std::shared_ptr<INetContext> ctx, std::string address_port,
        std::stop_token stp, _Filter flt) {
    static_assert(std::is_invocable_r_v<bool, _Filter, std::string>);
    TCPServer server(ctx, std::move(address_port));
    std::stop_callback _(stp, [&]{
        server.cancel();
    });
    while (!stp.stop_requested()) {
        auto awt = server.accept_handle();
        if (co_await awt.has_value()) {
            auto [handle, addr] = awt.await_resume();
            if (flt(std::move(addr))) {
                co_yield SocketStream::create(ctx, handle);
            } else {
                ctx->destroy(handle);
            }
        } else {
            break;
        }
    }
    co_return;

}

}
