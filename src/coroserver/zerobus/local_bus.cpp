#include "local_bus.hpp"
#include "mailbox_gen.hpp"
#include "thread_dispatcher.hpp"
#include "alloc_on_stack.hpp"

namespace zerobus {

static constexpr std::size_t disp_req_size
        =sizeof(LocalBus *) //this
        +sizeof(IListener *) //listener ptr
        +sizeof(Message)    //a message
        +sizeof(std::size_t); //additional argument


using Dispatcher = ThreadDispatcher<disp_req_size>;

LocalBus::LocalBus():_node_serial(LocalBus::get_random_channel_name({})) {
_cur_serial = _node_serial;
}

void LocalBus::notify_channel_change() {
    while (_channels_no_change.test_and_set(std::memory_order_relaxed)) {
        Dispatcher &disp = Dispatcher::get_instance();
        disp.enqueue([this, st = Channel::State()]() mutable{
            if (!st._locked) {
                _mx.lock_shared();
                st._locked = true;
            }
            std::size_t cnt = _monitors.size();
            while (st._pos < cnt) {
                IChannelNotifyListener *p = _monitors[st._pos];
                ++st._pos;
                p->on_channels_update();
            }
            if (st._locked) {
                _mx.unlock_shared();
                st._locked = false;
            }
        });
        disp.dispatch();
    }
}


bool LocalBus::subscribe(IListener *listener, ChannelID channel){
    return subscribe(listener, ChannelList(&channel,1));
}
bool LocalBus::subscribe(IListener *listener, ChannelList channelList){
    Dispatcher::get_instance().dispatch(); //finish any pending action
    bool result = true;
    {
        std::lock_guard _(_mx);
        for (const auto &chan: channelList) {
            std::shared_ptr<Channel> c = _public_channels.create_channel(chan, nullptr);
            if (c == nullptr) result = false;
            else {
                c->add(listener);
                _channels_no_change.clear(std::memory_order_relaxed);
            }
        }
    }
    notify_channel_change();
    return result;
}
void LocalBus::unsubscribe(IListener *listener, ChannelID channel){
    unsubscribe(listener, ChannelList(&channel,1));
}
void LocalBus::unsubscribe(IListener *listener, ChannelList channelList){
    Dispatcher::get_instance().dispatch(); //finish any pending action
    {
        std::lock_guard _(_mx);
        for (const auto &x: channelList) {
           std::shared_ptr<Channel> c = _public_channels.find_channel_for_broadcast(x, nullptr);
           if (c && c->remove(listener)) {
               _public_channels.erase(x);
               _channels_no_change.clear(std::memory_order_relaxed);
           }
        }
    }
    notify_channel_change();
}




bool LocalBus::is_valid_target_lk(const ChannelID &chan, IListener *sender) {
    return _private_channels.find(chan) != nullptr
            || _routing_cache.find_path(chan) != nullptr
            || _public_channels.find_channel_for_broadcast(chan, sender) != nullptr;
}

bool LocalBus::send_message(zerobus::IListener *listener,
        zerobus::ChannelID channel, zerobus::MessageContent content,
        zerobus::ConversationID cid) {

    bool fast = Dispatcher::get_instance().dispatch();
    std::shared_lock lk(_mx);

    if (!is_valid_target_lk(channel, listener)) return false;

    std::string id("~");
    std::string_view sender = _private_channels.find(listener);
    if (sender.empty()) {
        lk.unlock();
        std::unique_lock lk2(_mx);
        generate_mailbox_id(std::back_inserter(id));
        _private_channels.add(id, listener);
        sender = id;
        lk2.unlock();
        lk.lock();
    }
    return do_forward_message(lk, fast, listener, Message(sender, channel, content, cid));
}

bool LocalBus::do_forward_message(std::shared_lock<std::shared_mutex> &lk, bool fast, IListener *listener, const Message &msg) {
    auto &disp = Dispatcher::get_instance();
    if (fast && disp.empty()) {
        auto chan = msg.get_channel();
        bool pm = true;
        auto trg = _private_channels.find(chan);
        if (!trg) {
            trg = _routing_cache.find_path(chan);
            pm = false;
        }
        if (!trg) {
            auto c = _public_channels.find_channel_for_broadcast(chan, listener);
            if (!c) return false;
            lk.unlock();
            disp.enqueue([c, &msg, st = Channel::State()]()mutable{
               c->broadcast(msg, st);
            });
        } else {
            disp.enqueue([this, trg, &msg, pm, finish = false]()mutable{
               if (finish) {
                   _mx.unlock_shared();
                   return;
               }
               finish = true;
               trg->on_message(msg, pm);
               if (finish) {
                   _mx.unlock_shared();
                   finish = false;
               }
            });
            lk.release();
            disp.dispatch();
        }
    } else {
        //make copy of the message and send later
        disp.enqueue([this, msg = Message(msg), listener, finish = false]()mutable{
            if (finish) return;
            finish = true;
            auto &disp = Dispatcher::get_instance();
            disp.dispatch();
            std::shared_lock lk(_mx);
            do_forward_message(lk, true, listener, msg);
        });
    }
    return true;
}


bool LocalBus::forward_message(IListener *listener, const Message &msg) {
    bool fast = Dispatcher::get_instance().dispatch();
    std::shared_lock lk(_mx);
    auto chan = msg.get_channel();
    if (!is_valid_target_lk(chan, listener)) return false;
    return do_forward_message(lk, fast, listener, msg);

}

bool LocalBus::is_channel(ChannelID id) const {
    Dispatcher::get_instance().dispatch();
    std::shared_lock lk(_mx);
    auto c = _public_channels.find_channel_for_broadcast(id, nullptr);
    return static_cast<bool>(c);
}

void LocalBus::clear_path(ChannelID sender, ChannelID receiver) {
    Dispatcher &disp = Dispatcher::get_instance();
    disp.dispatch();    //finish pending, unlock all locks
    std::unique_lock lk(_mx);
    IListener *lsn = _routing_cache.find_path(sender);
    _routing_cache.clear_path(receiver);
    if (lsn) {
        disp.enqueue([this, lsn, sender, receiver, finish = false]() mutable{
            if (finish) {
                _mx.unlock();
                finish = false;
                return;
            }
            finish = true;
            lsn->on_no_route(sender, receiver);
            if (finish) _mx.unlock();
        });
        disp.dispatch();
    }
}

template<std::invocable<const Channel &> Pred>
ChannelList LocalBus::get_channels(ChannelListStorage &storage, Pred &&pred) const{
    Dispatcher::get_instance().dispatch();
    std::shared_lock lk(_mx);
    std::size_t need_cnt = 0;
    for (const auto &[chan, ptr]: _public_channels) {
        if (!pred(*ptr)) continue;
        ++need_cnt;
    }
    return alloc_on_stack<ChannelID>(need_cnt, [&](ChannelID *lst){
        std::size_t pos = 0;
        for (const auto &[chan, ptr]: _public_channels) {
            if (!pred(*ptr)) continue;
            lst[pos] = ChannelID(ptr->get_name());
            ++pos;
        }
        return storage.store_channels(ChannelList(lst, need_cnt));
    });
}

ChannelList LocalBus::get_subscribed_channels(const IListener *listener,
        ChannelListStorage &storage) const {
    return get_channels(storage, [&](const Channel &chan){
       return chan.get_owner() == nullptr && chan.contains(listener);
    });
}
ChannelList LocalBus::get_subscribed_groups(const IListener *listener,
        ChannelListStorage &storage) const {
    return get_channels(storage, [&](const Channel &chan){
       return chan.get_owner() != nullptr && chan.contains(listener);
    });
}
ChannelList LocalBus::get_public_channels(const IListener *listener,
        ChannelListStorage &storage) const {
    return get_channels(storage, [&](const Channel &chan){
       return chan.get_owner() == nullptr && !chan.contains(listener);
    });
}

void LocalBus::close_private_channel(IListener *listener) {
    Dispatcher::get_instance().dispatch();
    std::lock_guard _(_mx);
    _private_channels.erase(listener);

}

void LocalBus::unsubscribe_all(IListener *listener) {
    Dispatcher::get_instance().dispatch();
    std::unique_lock<std::shared_mutex> lk(_mx);
    _private_channels.erase(listener);
    _routing_cache.clear_bridge(listener);
    if (_serial_source == listener) {
        _cur_serial = _node_serial;
        _channels_no_change.clear(std::memory_order_relaxed);
    }
    unsubscribe_helper(lk, [&](auto &chan) {
        if constexpr(std::is_const_v<std::remove_reference_t<decltype(chan)> >) {
            return chan.get_owner() == listener || (chan.size() == 1 && chan.contains(listener));
        } else {
            return chan.get_owner() == listener || chan.remove(listener);
        }
    });
}

template<std::invocable<const Channel &> Pred>
void LocalBus::unsubscribe_helper(std::unique_lock<std::shared_mutex> &lk, Pred &&pred) {
    std::size_t needsz = 0;
    auto iter = _public_channels.begin();
    while (iter != _public_channels.end()) {
        if (pred(const_cast<const Channel &>(*iter->second))) ++needsz;
        ++iter;
    }
    alloc_on_stack<std::shared_ptr<Channel> >(needsz, [&](std::shared_ptr<Channel> *ptr){
        auto to_destroy = ptr;
        auto iter = _public_channels.begin();
        while (iter != _public_channels.end()) {
            if (pred(*iter->second)) {
                *to_destroy = std::move(iter->second);
                ++to_destroy;
                iter = _public_channels.erase(iter);
            } else {
                ++iter;
            }
        }
        lk.unlock();
        if (to_destroy != ptr) {
            _channels_no_change.clear(std::memory_order_relaxed);
        }
        //desructor of array deletes channels outside of lock
    });
    notify_channel_change();
}

void LocalBus::close_group(IListener *owner, ChannelID group_name) {
    Dispatcher::get_instance().dispatch();
    std::shared_ptr<Channel> c;
    {
        std::lock_guard lk(_mx);
        auto iter = _public_channels.find(group_name);
        if (iter == _public_channels.end()
                || iter->second->get_owner() != owner) return;
        c = std::move(iter->second);
        _public_channels.erase(iter);
    }
    //destructor of c deletes channel

}

bool LocalBus::add_to_group(IListener *owner, ChannelID group_name, ChannelID uid) {
    Dispatcher::get_instance().dispatch();
    std::lock_guard lk(_mx);
    IListener *trg = _private_channels.find(uid);
    if (!trg) trg = _routing_cache.find_path(uid);
    if (!trg) return false;
    auto c = _public_channels.create_channel(group_name, owner);
    if (!c) return false;
    c->add(trg);
    return true;
}

void LocalBus::channel_notify(IChannelNotifyListener *mon, bool enable) {
    Dispatcher::get_instance().dispatch();
    std::lock_guard lk(_mx);
    _monitors.erase(std::remove(_monitors.begin(), _monitors.end(), mon), _monitors.end());
    if (enable) {
        _monitors.push_back(mon);
    }
}

 void LocalBus::close_all_groups(IListener *owner) {
     Dispatcher::get_instance().dispatch();
     std::unique_lock<std::shared_mutex> lk(_mx);
     unsubscribe_helper(lk, [&](const Channel &chan) {
         return chan.get_owner() == owner;
     });
 }

 std::string LocalBus::get_random_channel_name(std::string_view prefix) const {
     std::string id(prefix);
     generate_mailbox_id(std::back_inserter(id));
     return id;

 }

bool LocalBus::update_serial(IListener *lsn, SerialID serialId) {
    Dispatcher::get_instance().dispatch();
    std::unique_lock lk(_mx);
    if (_cur_serial == serialId) {
        return _serial_source == lsn;
    }
    if (_cur_serial < serialId) {
        _cur_serial.clear();
        _cur_serial.append(serialId);
        _serial_source = lsn;
        lk.unlock();
        notify_channel_change();
    }
    return true;
}

SerialID LocalBus::get_serial() const {
    Dispatcher::get_instance().dispatch();
    std::shared_lock _(_mx);
    return _cur_serial;
}


}
