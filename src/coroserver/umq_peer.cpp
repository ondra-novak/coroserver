#include "umq_peer.h"
#include "umq_ws_connection.h"

#include <queue>
#include <sstream>

namespace coroserver {

namespace umq {



void Peer::toBase36(ID id, std::ostream &out) {
    char buff[50];
    char *c = buff;
    while (id) {
        auto p = id % 36;
        id = id / 36;
        if (p < 10) *c++ = '0' + p;
        else *c++ = 'A' + (p - 10);
    }
    while (c != buff) {
        --c;
        out.put(*c);
    }
}


Peer::ID Peer::fromBase36(std::string_view txt) {
    ID accum = 0;
    for (char c: txt) {
        if (c>='0' && c<='9') accum = accum * 36 + (c - '0');
        else if (c >= 'A' && c<='Z') accum = accum * 36 + (c - 'A' + 10);
        else throw std::invalid_argument("Invalid message ID");
    }
    return accum;
}


UMQException::UMQException(int code, std::string_view message):_code(code) {
    _msgtext = std::to_string(_code).append(" ").append(message);
    _message = std::string_view(_msgtext).substr(_msgtext.size()-message.size());
}

UMQException::UMQException(std::string_view txtmsg):_msgtext(txtmsg) {
    char *endptr;
    int code = strtol(_msgtext.c_str(), &endptr, 10);
    while (*endptr && std::isspace(*endptr)) ++endptr;
    _code = code;
    _message = std::string_view(endptr);
}

std::string_view UMQException::get_serialized() const {
    return _msgtext;
}

UMQException Peer::makeError(ErrorCode code) {
    return UMQException(static_cast<int>(code),strErrorCode[code]);
}

struct DownloadAttachment {
    coro::lazy_future<Peer::AttachmentData>::promise_target_type _lazy_target;
    coro::future<Peer::AttachmentData>::target_type _set_data_target;
    coro::promise<Peer::AttachmentData> _awaiting_attchment;
    coro::future<Peer::AttachmentData> _data;
    std::atomic<int> _state = {0};

    template<typename Fn>
    DownloadAttachment(Fn &&fn):_data(std::forward<Fn>(fn)) {}


    template<typename Fn>
    static coro::lazy_future<Peer::AttachmentData> start(Fn &&fn) {

        DownloadAttachment *inst = new DownloadAttachment(std::forward<Fn>(fn));
        coro::target_simple_activation(inst->_lazy_target, [inst](auto promise) {
            inst->_awaiting_attchment = std::move(promise);
            auto r = inst->_state.fetch_add(1);
            if (r) inst->finish();
        });
        coro::target_simple_activation(inst->_set_data_target, [inst](auto){
            auto r = inst->_state.fetch_add(1);
            if (r) inst->finish();
        });
        auto prom = inst->_data.get_promise();
        inst->_data.register_target(inst->_set_data_target);
        return inst->_lazy_target;
    }


    void finish() {
        _data.forward(std::move(_awaiting_attchment));
        delete this;
    }
};

class Peer::Core {
public:
    std::atomic<unsigned int> _refcnt = {2}; //instance + attachment runner
    std::unique_ptr<IConnection> _conn;
    coro::future<Message> _read_fut;
    coro::future<Message>::target_type _read_target;
    coro::promise<void> _done_promise;
    coro::promise<Request &&> _listen_promise;
    coro::promise<Payload &&> _connect_promise;
    coro::promise<Request &&> _request_promise;

    std::unordered_map<ID, coro::promise<Payload &&> > _requests;
    std::unordered_map<ID, coro::promise<Payload &&> > _subscriptions;
    std::unordered_map<ID, coro::promise<void> > _topics;
    std::unordered_map<ID, coro::promise<Payload &&> > _channels;
    std::mutex _mx;

    ID _next_id = 1;
    std::string _received_error;

    bool _reader_active = false;
    std::atomic<bool> _closed = {false};
    std::queue<coro::promise<AttachmentData> > _awaiting_attachments;

    std::mutex _buff_mx;
    std::ostringstream _out_buff;

    coro::queue<coro::lazy_future<AttachmentData> > _send_attachments;
    coro::lazy_future<AttachmentData>::target_type _resolve_attachment_target;
    coro::variant_future<coro::lazy_future<AttachmentData>, AttachmentData, bool> _send_attachments_fut;
    coro::lazy_future<AttachmentData> _cur_send_attachment_fut;
    AttachmentData _cur_send_attachment;
    coro::any_target<> _send_attachments_target;


    Core(std::unique_ptr<IConnection> &&conn):_conn(std::move(conn)) {
        auto &fut = _send_attachments_fut.as<coro::lazy_future<AttachmentData> >();
        fut << [&]{return _send_attachments.pop();};
        fut.register_target(_send_attachments_target.call([&](auto fut){
            queue_send_attach(fut);
        }));

    }

    void add_ref() {
        _refcnt.fetch_add(1,std::memory_order_acquire);
    }
    bool release_ref() {
        if (_refcnt.fetch_sub(1,  std::memory_order_release)<=1) {
            delete this;
            return false;
        }
        return true;
    }


    void start_receive();
    bool process_message(const Message &msg);
    void on_error_receive();

    void process_bin_message(std::string_view data);
    void process_text_message(std::string_view data);
    void execute_command(const Command &cmd, std::vector<coro::lazy_future<AttachmentData> > &&attachs);

    std::vector<coro::lazy_future<AttachmentData> > prepare_attachments(unsigned int count);


    void protocol_error(std::string_view text);

    void destroy();

    void on_error(PayloadWithID &payload);
    void on_attachment_error(PayloadWithID &payload);
    void on_hello(PayloadWithID &payload);
    void on_welcome(PayloadWithID &payload);
    void on_request(PayloadWithID &payload);
    void on_response(PayloadWithID &payload);
    void on_response_error(PayloadWithID &payload);
    void on_topic_update(PayloadWithID &payload);
    void on_unsubscribe(PayloadWithID &payload);
    void on_topic_close(PayloadWithID &payload);
    void on_channel_message(PayloadWithID &payload);

    coro::async<void> wait_for_accept(coro::promise<Payload &&> &prom);

    coro::lazy_future<bool> send_message(Type type, ID id, Payload &pl);
    coro::lazy_future<bool> send_message(Type type, ID id, Payload &&pl);
    void queue_send_attach(coro::future<coro::lazy_future<AttachmentData> > *res);
    void queue_send_attach_resolved(coro::future<AttachmentData> *res);
    void queue_send_attach_sent(coro::future<bool> *fut_res);
    void close();
    void cleanup();
};

void Peer::CoreDeleter::operator()(Core *core) {
        core->destroy();
};

void Peer::Core::start_receive() {

    coro::target_simple_activation(_read_target, [&](auto){

        try {
            while (true) {
                const Message &msg = _read_fut;
                bool rep = process_message(msg);
                if (!rep) {
                    release_ref();
                    return;
                }
                _read_fut << [&]{return _conn->receive();};
                if (_read_fut.register_target_async(_read_target)) return;
            }
        } catch (...) {
            on_error_receive();
            release_ref();
        }

    });
    _reader_active = true;
    add_ref();
    _read_fut << [&]{return _conn->receive();};
    _read_fut.register_target(_read_target);
}

bool Peer::Core::process_message(const Message &msg) {
    switch (msg._type) {
        case MessageType::close: {
                std::lock_guard _(_mx);
                _reader_active = false;
            }
            _done_promise();
            return false;
        case MessageType::binary:
            process_bin_message(msg._payload);
            return true;
        case MessageType::text:
            process_text_message(msg._payload);
            return true;
        default:
            return true;
    }
}

void Peer::Core::on_error_receive() {
    _reader_active = false;
    _done_promise.reject();
}

void Peer::Core::process_bin_message(std::string_view data) {
    if (_awaiting_attachments.empty()) {
        protocol_error("Unexpected binary message");
        return;
    }
    auto p = std::move(_awaiting_attachments.front());
    _awaiting_attachments.pop();
    p(data.begin(), data.end());

}

struct Peer::Command {
    Type type = Type::error;
    ID id = 0;
    std::string_view payload;

    Command() = default;
    Command(std::string_view data) {
        auto sep = data.find(':');
        if (sep == data.npos || sep == 0) {
            throw std::invalid_argument("Invalid message format");
        } else {
            type = static_cast<Type>(data[0]);
            id = fromBase36(data.substr(1, sep-1));
            payload = data.substr(sep+1);

        }
    }
    void build(std::ostream &out) {
        out << static_cast<char>(type);
        toBase36(id, out);
        out << ':' << payload;
    }
    Command(Type type, ID id, std::string_view payload)
        :type(type),id(id),payload(payload) {}

};

void Peer::Core::process_text_message(std::string_view data) {
    try {
        Command cmd(data);

        if (cmd.type == Type::attachment_count) {
            execute_command(Command(cmd.payload), prepare_attachments(cmd.id));
        } else {
            execute_command(cmd, {});
        }

    } catch (const std::exception &e) {
        protocol_error(e.what());
    }
}


void Peer::Core::execute_command(const Command &cmd, std::vector<coro::lazy_future<AttachmentData> > &&attachs) {
    PayloadWithID pl;
    pl.text = cmd.payload;
    pl.id = cmd.id;
    pl.attachments = std::move(attachs);
    switch (cmd.type) {
        case Type::error: on_error(pl); break;
        case Type::attachment_error: on_attachment_error(pl); break;
        case Type::hello: on_hello(pl); break;
        case Type::welcome: on_welcome(pl); break;
        case Type::request: on_request(pl); break;
        case Type::response: on_response(pl); break;
        case Type::response_error: on_response_error(pl); break;
        case Type::topic_update: on_topic_update(pl); break;
        case Type::unsubscribe: on_unsubscribe(pl); break;
        case Type::topic_close: on_topic_close(pl); break;
        case Type::channel_message: on_channel_message(pl); break;
        default: protocol_error("Unsupported frame type"); break;
    }
}

void Peer::Core::protocol_error(std::string_view text) {
    send_message(Type::error, 0, Payload(text));
    close();
}

std::vector<coro::lazy_future<Peer::AttachmentData> > Peer::Core::prepare_attachments(unsigned int count) {
    std::vector<coro::lazy_future<Peer::AttachmentData> > out;
    for (unsigned int i = 0; i < count; ++i) {
        out.push_back(DownloadAttachment::start([&](auto promise) {
                _awaiting_attachments.push(std::move(promise));
        }));
    }
    return out;
}

void Peer::Core::destroy() {
    close();
    cleanup();
    release_ref();
}

coro::lazy_future<bool> Peer::Core::send_message(Type type, ID id, Payload &pl) {

    std::lock_guard _(_buff_mx);
    if (!pl.attachments.empty()) {
        Command cmd(Type::attachment_count, pl.attachments.size(), {});
        cmd.build(_out_buff);
    }
    Command(type, id, pl.text).build(_out_buff);
    auto ret = _conn->send({MessageType::text, _out_buff.view()});
    _out_buff.str({});
    for (auto &x: pl.attachments) {
        _send_attachments.push(std::move(x));
    }

    return ret;

}

coro::lazy_future<bool> Peer::Core::send_message(Type type, ID id, Payload &&pl) {
    return send_message(type,id, pl);
}

void Peer::Core::queue_send_attach(coro::future<coro::lazy_future<AttachmentData> > *res) {
    try {
        _cur_send_attachment_fut = std::move(res->get());
        _cur_send_attachment_fut.register_target(_send_attachments_target.call([&](auto fut){
            queue_send_attach_resolved(fut);
        }));
    } catch (...) {
        release_ref(); //only broken promise;
        return;
    }
}
void Peer::Core::queue_send_attach_resolved(coro::future<AttachmentData> *res) {
    try {
        _cur_send_attachment = std::move(res->get());
        auto &fut = _send_attachments_fut.as<bool>();
        fut << [&]()->coro::future<bool> {return _conn->send({MessageType::binary, {_cur_send_attachment.data(), _cur_send_attachment.size()}});};
        fut.register_target(_send_attachments_target.call([&](auto fut){
            queue_send_attach_sent(fut);
        }));
    } catch (std::exception &e) {
        auto &fut = _send_attachments_fut.as<bool>();
        fut << [&]()->coro::future<bool> {
            return send_message(Type::attachment_error,0,Payload{e.what()});
        };
        fut.register_target(_send_attachments_target.call([&](auto fut){
            queue_send_attach_sent(fut);
        }));
    }
}
void Peer::Core::queue_send_attach_sent(coro::future<bool> *) {
    try {
        auto &fut = _send_attachments_fut.as<coro::lazy_future<AttachmentData> >();
        fut << [&]{return _send_attachments.pop();};
        fut.register_target(_send_attachments_target.call([&](auto fut){
            queue_send_attach(fut);
        }));
    } catch (...) {
        release_ref();
        return;
    }

}


void Peer::Core::cleanup() {
    decltype(_requests) requests;
    decltype(_subscriptions) subscriptions;
    decltype(_channels) channels;
    decltype(_topics) topics;
    {
        std::lock_guard lk(_mx);
        requests = std::move(_requests);
        subscriptions = std::move(_subscriptions);
        channels = std::move(_channels);
        topics = std::move(_topics);
    }
    _done_promise.drop();
    _listen_promise.drop();
    _connect_promise.drop();
    _request_promise.drop();
    _send_attachments.close();

    for (auto &[x,promise]: topics) {
        promise();
    }
    //dtors

}

void Peer::Core::on_error(PayloadWithID &payload) {
    std::unique_lock lk(_mx);
    _received_error = payload.text;
    auto a = _done_promise.reject(ProtocolError(_received_error));
    auto b = _listen_promise.reject(ProtocolError(_received_error));
    auto c = _connect_promise.reject(ProtocolError(_received_error));
    lk.unlock();
}


void Peer::Core::on_attachment_error(PayloadWithID &payload) {
    if (_awaiting_attachments.empty()) {
        protocol_error("No attachment is expected");
        return;
    }
    auto p = std::move(_awaiting_attachments.front());
    _awaiting_attachments.pop();
    p.reject(AttachmentError(payload.text));
}

coro::async<void> Peer::Core::wait_for_accept(coro::promise<Payload &&> &prom) {
    add_ref();
    coro::future<Payload &&> response;
    prom = response.get_promise();
    try {
        Payload pl = co_await response;
        send_message(Type::welcome, version, pl);
    } catch (const coro::broken_promise_exception &e) {
        protocol_error("Rejected");
    } catch (const std::exception &e) {
        protocol_error(e.what());
    }
    release_ref();
}

void Peer::Core::on_hello(PayloadWithID &payload) {
    if (payload.id != version) {
        protocol_error("Unsupported version");
        return;
    }
    coro::promise<Payload &&> prom;
    wait_for_accept(prom).detach();
     _listen_promise(std::move(prom), std::move(payload));
}

void Peer::Core::on_welcome(PayloadWithID &payload) {
    if (payload.id != version) {
        protocol_error("Unsupported version");
        _connect_promise.reject(ProtocolError("Rejected because unsupported version"));
        return;
    }
    _connect_promise(std::move(payload));
}

struct Peer::OpenedRequest {
    ID _id = 0;
    coro::future<Payload &&> _fut;
    coro::future<Payload &&>::target_type _target;
    Core *_owner;

    OpenedRequest(Core *core):_owner(core) {
        _owner->add_ref();
        coro::target_simple_activation(_target, [&](auto *f){
            process_response(f);
        });
    }

    OpenedRequest(const OpenedRequest &) = delete;
    OpenedRequest &operator=(const OpenedRequest &) = delete;

    ~OpenedRequest() {
        _owner->release_ref();
    }

    void process_response(coro::future<Payload &&>  *fut) {
        try {
            Payload pl = fut->get();
            _owner->send_message(Type::response, _id, std::move(pl));
        } catch (std::exception &e) {
            _owner->send_message(Type::response_error, _id, Payload(e.what()));
        }
        coro::pool_alloc<OpenedRequest>::instance().destroy(this);
    }

    static OpenedRequest *create(Core *owner) {
        return coro::pool_alloc<OpenedRequest>::instance().construct(owner);
    }

};

void Peer::Core::on_request(PayloadWithID &payload) {
    if (_request_promise) { //awaiting request?
        auto open_req = OpenedRequest::create(this);
        open_req->_id = payload.id;
        auto prom = open_req->_fut.get_promise();
        open_req->_fut.register_target(open_req->_target);

        if (_request_promise(std::move(prom), std::move(payload))) return;

        prom();
    }
    send_message(Type::response_error, payload.id, Payload("Unexpected request"));
}

void Peer::Core::on_response(PayloadWithID &payload) {
    coro::promise<Payload &&>::pending_notify ntf;
    {
        std::lock_guard _(_mx);
        auto iter = _requests.find(payload.id);
        if (iter == _requests.end()) return;
        ntf = iter->second(std::move(payload));
        _requests.erase(iter);
    }
    //ntf destructor
}

void Peer::Core::on_response_error(PayloadWithID &payload) {
    coro::promise<Payload &&>::pending_notify ntf;
    {
        std::lock_guard _(_mx);
        auto iter = _requests.find(payload.id);
        if (iter == _requests.end()) return;
        ntf = iter->second.reject(RequestError(payload.text));
        _requests.erase(iter);
    }
    //ntf destructor

}

void Peer::Core::on_topic_update(PayloadWithID &payload) {
    coro::promise<Payload &&>::pending_notify ntf;
    std::lock_guard _(_mx);
    auto iter = _subscriptions.find(payload.id);
    if (iter == _subscriptions.end()) {
        send_message(Type::unsubscribe, payload.id, {});
    } else {
        ntf = iter->second(std::move(payload));
    }
}

void Peer::Core::on_unsubscribe(PayloadWithID &payload) {
    coro::promise<void>::pending_notify ntf;
    std::lock_guard _(_mx);
    auto iter = _topics.find(payload.id);
    if (iter == _topics.end()) return;
    ntf = iter->second();
    _topics.erase(iter);
}

void Peer::Core::on_topic_close(PayloadWithID &payload) {
    coro::promise<Payload &&>::pending_notify ntf;
    std::lock_guard _(_mx);
    auto iter = _subscriptions.find(payload.id);
    if (iter == _subscriptions.end()) return;
    ntf = iter->second.drop();
}

void Peer::Core::on_channel_message(PayloadWithID &payload) {
    coro::promise<Payload &&>::pending_notify ntf;
    std::lock_guard _(_mx);
    auto iter = _channels.find(payload.id);
    if (iter == _channels.end()) return;
    ntf = iter->second(std::move(payload));
    _channels.erase(iter);
}

inline void Peer::Core::close() {
    if (_closed.exchange(true)) return;
    _conn->send({MessageType::close});
}


coro::future<Peer::Payload &&> Peer::connect(PConnection &&conn, Payload &&pl) {
    _core = PCore(new Core(std::move(conn)));
    return [&](auto prom) {
        _core->_connect_promise = std::move(prom);
        _core->start_receive();
        _core->send_message(Type::hello, version, std::move(pl));
    };
}

coro::future<Peer::Request &&> Peer::listen(PConnection &&conn) {
    _core = PCore(new Core(std::move(conn)));
    return [&](auto prom) {
        _core->_listen_promise = std::move(prom);
        _core->start_receive();
    };
}

coro::future<void> Peer::done() {
    return [&](auto prom) {
        if (!_core) return;
        std::unique_lock lk(_core->_mx);
        if (!_core->_received_error.empty()) {
            auto x = prom.reject(ProtocolError(_core->_received_error));
            lk.unlock();
            return;
        }
        if (!_core->_reader_active) {
            auto x = prom();
            lk.unlock();
            return;
        }
        _core->_done_promise = std::move(prom);
    };
}


coro::future<Peer::Payload &&> Peer::send_request(Payload &request) {
    return [&](auto promise) {
        ID id;
        {
            if (!_core) return;
            std::lock_guard _(_core->_mx);
            id = _core->_next_id++;
            _core->_requests.emplace(id, std::move(promise));
        }
        _core->send_message(Type::request, id, request);
    };
}

coro::future<Peer::Payload &&> Peer::send_request(Payload &&request) {
    return send_request(request);
}

coro::future<Peer::Request &&> Peer::receive_request() {
    return [&](auto prom) {
        if (!_core) return;
        _core->_request_promise = std::move(prom);
    };
}

Peer::Subscription Peer::subscribe() {
    std::lock_guard _(_core->_mx);
    if (!_core) throw std::logic_error("Inactive peer");
    ID id = _core->_next_id++;
    _core->_subscriptions[id];
    return Subscription(id, _core.get());
}

Peer::Subscription::~Subscription() {
    coro::promise<Payload &&>::pending_notify ntf;
    std::lock_guard _(_core->_mx);
    auto iter = _core->_subscriptions.find(_id);
    if (iter == _core->_subscriptions.end()) return;
    ntf = iter->second.drop();
    _core->_subscriptions.erase(iter);
}

coro::future<Peer::Payload &&> Peer::Subscription::receive() {
    return [&](auto prom) {
        std::lock_guard _(_core->_mx);
        auto iter = _core->_subscriptions.find(_id);
        if (iter == _core->_subscriptions.end()) return;
        iter->second = std::move(prom);
    };
}

Peer::SubscriptionBase::SubscriptionBase(ID id, Core *core)
    :_id(id),_core(core) {
    _core->add_ref();
}

void Peer::SubscriptionCoreDeleter::operator ()(Core *x) const {
    x->release_ref();
}

Peer::Topic Peer::publish(ID topic_id) {
    if (!_core) throw std::logic_error("Inactive peer");
    std::lock_guard _(_core->_mx);
    _core->_topics[topic_id];
    return Topic(topic_id, _core.get());
}

Peer::Topic::~Topic() {
    coro::promise<void>::pending_notify ntf;
    {
        std::lock_guard _(_core->_mx);
        auto iter = _core->_topics.find(_id);
        if (iter == _core->_topics.end()) return;
        ntf = iter->second.drop();
        _core->_topics.erase(iter);
    }
    _core->send_message(Type::topic_close, _id, {});

}

coro::lazy_future<bool> Peer::Topic::publish(Payload &pl) {
    {
        std::lock_guard _(_core->_mx);
        auto iter = _core->_topics.find(_id);
        if (iter ==_core->_topics.end()) return false;
    }
    return _core->send_message(Type::topic_update, _id, pl);
}

coro::lazy_future<bool> Peer::Topic::publish(Payload &&pl) {
    return publish(pl);
}

coro::future<void> Peer::Topic::on_unsubscribe() {
    return [&](auto promise) {
        std::lock_guard _(_core->_mx);
        auto iter = _core->_topics.find(_id);
        if (iter ==_core->_topics.end()) return;
        iter->second = std::move(promise);
    };
}

coro::lazy_future<bool> Peer::channel_send(ID channel_id, Payload &pl) {
    if (!_core) return false;
    return _core->send_message(Type::channel_message, channel_id, pl);
}
coro::lazy_future<bool> Peer::channel_send(ID channel_id, Payload &&pl) {
    if (!_core) return false;
    return _core->send_message(Type::channel_message, channel_id, std::move(pl));
}

coro::future<Peer::Payload &&> Peer::channel_receive(ID channel_id) {
    return [&](auto promise) {
        if (!_core) return;
        std::lock_guard _(_core->_mx);
        _core->_channels[channel_id] = std::move(promise);
    };
}

Peer::Peer() {
}

Peer::State Peer::get_state() const {
    if (_core == nullptr) return State::unbound;
    if (_core->_listen_promise || _core->_connect_promise) return State::opening;
    if (_core->_closed) return State::closing;
    if (!_core->_reader_active) return State::closed;
    return State::open;
}



}

}

