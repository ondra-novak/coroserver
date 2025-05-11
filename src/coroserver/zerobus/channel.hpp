#pragma once

#include <string>
#include "message.hpp"
#include "listener.hpp"
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <algorithm>
#include <unordered_map>

namespace zerobus
{

class Channel
{
public:
    /// default constructor
    Channel(const std::string_view &name, IListener *owner = nullptr) : _name(std::move(name)), _owner(owner) {}

    /// Retrieve owner of the channel
    IListener *get_owner() const { return _owner; }
    /// Retrieve channel name
    std::string_view get_name() const { return _name; }

    struct State {
        std::size_t _pos = 0;
        bool _locked = false;
    };

    void add(IListener *lsn)
    {
        std::unique_lock lock(_mx);
        if (!is_subscribed_lk(lsn))
        {
            _listeners.push_back(lsn);
        }
    }

    bool contains(const IListener *lsn) const {
        std::shared_lock _(_mx);
        return std::find(_listeners.begin(), _listeners.end(), lsn) != _listeners.end();
    }

    bool remove(const IListener *lsn)
    {
        std::unique_lock lock(_mx);
        _listeners.erase(std::remove_if(_listeners.begin(), _listeners.end(),
                                        [&](const IListener *l)
                                        { return l == lsn; }),
                            _listeners.end());
        return _listeners.empty();
    }

    void broadcast(const Message &msg, State &state)
    {
        if (!state._locked) {
            _mx.lock_shared();
            state._locked = true;
        }
        std::size_t cnt = _listeners.size();
        while (state._pos < cnt) {
            auto &p = _listeners[state._pos];
            ++state._pos;
            p->on_message(msg, false);
        }

        if (state._locked) {
            _mx.unlock_shared();
            state._locked = false;
        }
    }

    bool empty() const
    {
        std::shared_lock lock(_mx);
        return _listeners.empty();
    }

    std::size_t size() const {
        std::shared_lock lock(_mx);
        return _listeners.size();
    }

    void close_group()
    {
        std::unique_lock lock(_mx);
        for (const auto &item : _listeners)
        {
            item->on_close_group(_name);
        }
        _listeners.clear();
    }
    ~Channel()
    {
        bool is_empty = _listeners.empty();
        if (!is_empty) {
            close_group();
        } else if (_owner) {
            _owner->on_group_empty(_name);
        }
    }

    Channel(const Channel &) = delete;
    Channel &operator=(const Channel &) = delete;

    /// Creates new channel
    /**
     * @param name of the channel
     * @param owner if paramater is nullptr, public channel has been created,
     *              otherwise a group is created
     */
    static std::shared_ptr<Channel> create(const std::string_view &name, IListener *owner = nullptr)
    {
        return std::make_shared<Channel>(name, owner);
    }

protected:
    std::vector<IListener *> _listeners;
    std::string _name;
    IListener *_owner;
    mutable std::shared_mutex _mx;

    bool is_subscribed_lk(IListener *lsn) const
    {
        return std::find(_listeners.begin(), _listeners.end(), lsn) != _listeners.end();
    }
};

class PublicChannelMap: public std::unordered_map<std::string_view, std::shared_ptr<Channel> > {
public:

    ///registers channel
    /**
     * @param name name of channel
     * @param owner if nullptr then public channel, if not nullptr, then group is
     * created.
     *
     * @return shared pointer to channel
     *
     * @note if channel is already exists, then function returns existing
     * channel. However it also checks for ownership. If ownership mismatch,
     * return value is nullptr
     *
     */
    std::shared_ptr<Channel> create_channel(std::string_view name, IListener *owner = nullptr) {
        auto iter = find(name);
        if (iter != end()) {
            if (owner != iter->second->get_owner()) return {};
            return iter->second;
        }
        auto ptr = Channel::create(name, owner);
        emplace(ptr->get_name(), ptr);
        return ptr;
    }

    std::shared_ptr<Channel> find_channel_for_broadcast(std::string_view name, IListener *sender) const {
        auto iter = find(name);
        if (iter == end() || sender != iter->second->get_owner()) return {};
        return iter->second;
    }

};

class PrivateChannelMap {
public:

    IListener * find(std::string_view channel) const {
        auto iter = _channel2listener.find(channel);
        return iter != _channel2listener.end()?iter->second:nullptr;
    }
    std::string_view find(IListener *listener) {
        auto iter = _listener2channel.find(listener);
        return iter != _listener2channel.end()?iter->second:std::string_view();
    }

    bool add(std::string_view channel, IListener *listener) {
        auto st = _listener2channel.emplace(listener, std::string(channel));
        if (!st.second) return false;
        std::string_view n = st.first->second;
        _channel2listener.emplace(n, listener);
        return true;
    }

    void erase(std::string_view channel) {
        auto iter = _channel2listener.find(channel);
        auto iter2 = _listener2channel.find(iter->second);
        _channel2listener.erase(iter);
        _listener2channel.erase(iter2);
    }

    void erase(const IListener *listener){
        auto iter = _listener2channel.find(listener);
        auto iter2 = _channel2listener.find(iter->second);
        _channel2listener.erase(iter2);
        _listener2channel.erase(iter);
    }

protected:
    std::unordered_map<const IListener *, std::string> _listener2channel;
    std::unordered_map<std::string_view, IListener *> _channel2listener;


};


}
