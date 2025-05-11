#pragma once

#include "bus.hpp"
#include "channel.hpp"
#include "routing_cache.h"
#include <unordered_map>
#include <queue>
#include <atomic>



namespace zerobus {


class LocalBus : public IBus {
public:

    LocalBus();

    virtual bool subscribe(IListener *listener, ChannelID channel) override;
    virtual bool subscribe(IListener *listener, ChannelList channel)override;
    virtual void unsubscribe(IListener *listener, ChannelID channel)override;
    virtual void unsubscribe(IListener *listener, ChannelList channel)override;
    virtual bool send_message(IListener *listener,ChannelID channel,
            MessageContent msg, ConversationID cid) override;
    virtual bool forward_message(IListener *listener, const Message &msg) override;
    virtual bool is_channel(ChannelID id) const override;
    virtual bool update_serial(IListener *lsn, SerialID serialId) override;
    virtual void clear_path(ChannelID sender, ChannelID receiver) override;
    virtual ChannelList get_public_channels(
            const IListener *listener, ChannelListStorage &storage) const override;
    virtual ChannelList get_subscribed_channels(const IListener *listener,
            ChannelListStorage &storage) const override;
    virtual ChannelList get_subscribed_groups(
            const IListener *listener, ChannelListStorage &storage) const override;
    virtual void close_private_channel(IListener *listener) override;
    virtual void unsubscribe_all(IListener *listener) override;
    virtual void close_group(IListener *owner, ChannelID group_name) override;
    virtual bool add_to_group(IListener *owner, ChannelID group_name, ChannelID uid) override;
    virtual void channel_notify(IChannelNotifyListener *mon, bool enable) override;
    virtual SerialID get_serial() const override;
    virtual void close_all_groups(IListener *owner) override;
    virtual std::string get_random_channel_name(std::string_view prefix) const override;

protected:


PublicChannelMap _public_channels;
PrivateChannelMap _private_channels;
RoutingCache _routing_cache;
std::vector<IChannelNotifyListener *> _monitors;
mutable std::shared_mutex _mx;
std::atomic_flag _channels_no_change = {false};
std::string _node_serial = {};
std::string _cur_serial = {};
IListener *_serial_source = nullptr;

bool do_forward_message(std::shared_lock<std::shared_mutex> &lk, bool fast, IListener *listener, const Message &msg);


bool is_valid_target_lk(const ChannelID &chan, IListener *sender);
void notify_channel_change();

template<std::invocable<const Channel &>Pred>
ChannelList get_channels(ChannelListStorage &storage, Pred &&pred) const;
template<std::invocable<const Channel &> Pred>
void unsubscribe_helper(std::unique_lock<std::shared_mutex> &lk, Pred &&pred);

};

}

