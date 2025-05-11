#include "abstract_bridge.hpp"

namespace zerobus {


AbstractBridge::AbstractBridge(Bus bus):_bus(std::move(bus)) {
    _bus.channel_notify(this, true);
}

AbstractBridge::~AbstractBridge() {
    _bus.channel_notify(this, false);
    _bus.unsubscribe_all(this);
}

void AbstractBridge::on_channels_update() noexcept {
    //lock and read requests
    auto n = _lk_flag.exchange(chan_locked);
    do {
        if (n & chan_locked) {   //already locked?
            n |= chan_need_update;  //add flag that we need update
            n = _lk_flag.exchange(n); //try to update flag
            if (n & chan_locked) return; //if still locked, we done
        }
        //locked for us
        //if reset requested, do reset
        if (n & chan_need_reset) {
            _cur_list.store_channels({});
        }
        //perform update
        on_channels_update_lk();
        //unlock (set zero), read requests
        n = _lk_flag.exchange(0);
        //there should be no requests exit
        if (n == chan_locked) return;
        //if there are requests, lock it back and repeat
        n = _lk_flag.exchange(chan_locked);
    } while (true);

}

void AbstractBridge::on_channels_update_lk() noexcept {
    auto srl = _bus.get_serial();
    if (srl != _serial_id) {
        _serial_id = srl;
        handle_outgoing_msg(MsgUpdateSerial{srl});
    }

    //if cycle detected, do not propagate channels to other side
    if (_cycle_status.load(std::memory_order_relaxed)) {
        handle_outgoing_msg(MsgSetChannels{});
        return;
    }

    ChannelList new_lst = _bus.get_public_channels(this,_tmp_list);
    new_lst = _tmp_list.make_ordered();
    ChannelList old_lst = _cur_list.get_stored();
    if (_cur_list.get_stored().empty()) {
        std::swap(_cur_list, _tmp_list);
        handle_outgoing_msg(MsgSetChannels{new_lst});
        return;
    }
    ChannelList added = _diff_list.set_difference(old_lst, new_lst);
    if (!added.empty()) {
        handle_outgoing_msg(MsgAddChannels{added});
    }
    ChannelList removed = _diff_list.set_difference(new_lst, old_lst);
    if (!removed.empty()) {
        handle_outgoing_msg(MsgEraseChannels{removed});
    }
    std::swap(_cur_list, _tmp_list);
}

void AbstractBridge::on_close_group(ChannelID group_name) noexcept {
    handle_outgoing_msg(MsgCloseGroup{group_name});
}

void AbstractBridge::on_no_route(ChannelID sender, ChannelID receiver) noexcept{
    handle_outgoing_msg(MsgNoRoute{sender, receiver});
}

void AbstractBridge::on_group_empty(ChannelID group_name) noexcept{
    handle_outgoing_msg(MsgGroupEmpty{group_name});
}

void AbstractBridge::on_add_to_group(ChannelID group_name, ChannelID target_id) noexcept{
    handle_outgoing_msg(MsgAddToGroup{group_name, target_id});
}

void AbstractBridge::on_message(const Message &message, bool pm) noexcept{
    if (!pm) handle_outgoing_msg(message);
    else _bus.clear_path(message.get_sender(), message.get_channel());
}

void AbstractBridge::handle_incoming_msg(const Message &msg) noexcept{
    if (!_bus.forward_message(this, msg)) {
        _bus.clear_path(msg.get_sender(), msg.get_channel());
    }
}

void AbstractBridge::handle_incoming_msg(const MsgSetChannels &msg) noexcept {
    ChannelListStorage tmp_list;
    ChannelListStorage diff_list;
    ChannelList cur_lst = _bus.get_subscribed_channels(this,tmp_list);
    cur_lst = tmp_list.make_ordered();
    ChannelList new_lst = msg.lst;
    ChannelList added = diff_list.set_difference(cur_lst, new_lst);
    if (!added.empty()) {
        _bus.subscribe(this, added);
    }
    ChannelList removed = diff_list.set_difference(new_lst, cur_lst);
    if (!removed.empty()) {
        _bus.unsubscribe(this, added);
    }
}

void AbstractBridge::handle_incoming_msg(const MsgAddChannels &msg) noexcept {
    _bus.subscribe(this, msg.lst);
}

void AbstractBridge::handle_incoming_msg(const MsgEraseChannels &msg) noexcept {
    _bus.unsubscribe(this, msg.lst);
}

void AbstractBridge::handle_incoming_msg(const MsgUpdateSerial &msg) noexcept {
    bool has_cycle = !_bus.update_serial(this, msg.serial);
    bool pstate = _cycle_status.exchange(has_cycle, std::memory_order_relaxed);
    if (pstate != has_cycle) {
        on_channels_update();
    }

}

void AbstractBridge::handle_incoming_msg(const MsgChannelReset &) noexcept {
    //request to need reset channels
    _lk_flag.fetch_or(chan_need_reset);
    //perform update
    on_channels_update_lk();
}

void AbstractBridge::handle_incoming_msg(const MsgNewSession &) noexcept {
    handle_incoming_msg(MsgChannelReset{});
}

void AbstractBridge::handle_incoming_msg(const MsgNoRoute &msg) noexcept {
    _bus.clear_path(msg.sender, msg.receiver);
}

void AbstractBridge::handle_incoming_msg(const MsgCloseGroup &msg) noexcept {
    _bus.close_group(this, msg.group);
}

void AbstractBridge::handle_incoming_msg(const MsgGroupEmpty &msg) noexcept {
    _bus.unsubscribe(this, msg.group);
}

void AbstractBridge::handle_incoming_msg(const MsgAddToGroup &msg) noexcept {
    _bus.add_to_group(this, msg.group, msg.target);
}

void AbstractBridge::send_reset() {
    handle_outgoing_msg(MsgChannelReset{});
}

void AbstractBridge::send_new_session(unsigned long version) {
    handle_outgoing_msg(MsgNewSession{version});
}


}
