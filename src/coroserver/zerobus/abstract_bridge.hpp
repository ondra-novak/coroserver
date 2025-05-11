#pragma once
#include <atomic>
#include "bus.hpp"
#include "channel_notify_listener.hpp"
#include "listener.hpp"

namespace zerobus {

class AbstractBridge: public IListener, public IChannelNotifyListener {
public:


    /// @brief Represents a message to set a list of channels.
    /// This message requires unsubscribing from all current channels and subscribing to the new list.
    struct MsgSetChannels {
        ChannelList lst; ///< List of channels to set.
    };

    /// @brief Represents a message to add a list of channels.
    struct MsgAddChannels {
        ChannelList lst; ///< List of channels to add.
    };

    /// @brief Represents a message to erase a list of channels.
    struct MsgEraseChannels {
        ChannelList lst; ///< List of channels to erase.
    };

    /// @brief Represents a message to update the serial identifier.
    struct MsgUpdateSerial {
        std::string_view serial; ///< The new serial identifier.
    };

    /// @brief This message is sent from the other side to indicate that all channels
    /// have been unsubscribed due to some reason. It requests the current channel
    /// list to be resent.
    struct MsgChannelReset {};

    /// @brief Represents a message to start a new session.
    struct MsgNewSession {
        unsigned long version = 1; ///< Version of the new session.
    };

    /// @brief Represents a message indicating no route exists between sender and receiver.
    struct MsgNoRoute {
        ChannelID sender;   ///< The sender channel ID.
        ChannelID receiver; ///< The receiver channel ID.
    };

    /// @brief Represents a message to close a specific group.
    struct MsgCloseGroup {
        ChannelID group; ///< The group ID to close.
    };

    /// @brief Represents a message indicating a group is empty and has been closed
    struct MsgGroupEmpty {
        ChannelID group; ///< The empty group ID.
    };

    /// @brief Represents a message to add a target channel to a group.
    struct MsgAddToGroup {
        ChannelID group;  ///< The group ID.
        ChannelID target; ///< The target channel ID to add to the group.
    };


    AbstractBridge(Bus _bus);
    ~AbstractBridge();

    AbstractBridge(const AbstractBridge &) = delete;
    AbstractBridge &operator=(const AbstractBridge &) = delete;

    virtual void on_channels_update() noexcept override;
    virtual void on_close_group(ChannelID group_name) noexcept override;
    virtual void on_no_route(ChannelID sender,ChannelID receiver) noexcept override;
    virtual void on_group_empty(ChannelID group_name) noexcept override;
    virtual void on_add_to_group(ChannelID group_name,ChannelID target_id) noexcept override;
    virtual void on_message(const Message &message, bool pm) noexcept override;


    void send_reset();
    void send_new_session(unsigned long version);
    unsigned long get_version() const {return _version;}

    void handle_incoming_msg(const Message &msg) noexcept;
    void handle_incoming_msg(const MsgSetChannels &msg) noexcept;
    void handle_incoming_msg(const MsgAddChannels &msg) noexcept;
    void handle_incoming_msg(const MsgEraseChannels &msg) noexcept;
    void handle_incoming_msg(const MsgUpdateSerial &msg) noexcept;
    void handle_incoming_msg(const MsgChannelReset &msg) noexcept;
    void handle_incoming_msg(const MsgNewSession &msg) noexcept;
    void handle_incoming_msg(const MsgNoRoute &msg) noexcept;
    void handle_incoming_msg(const MsgCloseGroup &msg) noexcept;
    void handle_incoming_msg(const MsgGroupEmpty &msg) noexcept;
    void handle_incoming_msg(const MsgAddToGroup &msg) noexcept;
protected:

    Bus _bus;
    std::string _serial_id;
    ChannelListStorage _cur_list;
    ChannelListStorage _tmp_list;
    ChannelListStorage _diff_list;
    unsigned long _version = 0;
    std::atomic<unsigned char> _lk_flag = {0};
    std::atomic<unsigned char> _cycle_status = {false};
    static constexpr unsigned char chan_locked = 1;
    static constexpr unsigned char chan_need_update = 2;
    static constexpr unsigned char chan_need_reset = 4;

    //overrides
    virtual void handle_outgoing_msg(const Message &msg) noexcept = 0;
    virtual void handle_outgoing_msg(const MsgSetChannels &msg) noexcept = 0;
    virtual void handle_outgoing_msg(const MsgAddChannels &msg) noexcept = 0;
    virtual void handle_outgoing_msg(const MsgEraseChannels &msg) noexcept = 0;
    virtual void handle_outgoing_msg(const MsgUpdateSerial &msg) noexcept = 0;
    virtual void handle_outgoing_msg(const MsgChannelReset &msg) noexcept = 0;
    virtual void handle_outgoing_msg(const MsgNewSession &msg) noexcept = 0;
    virtual void handle_outgoing_msg(const MsgNoRoute &msg) noexcept = 0;
    virtual void handle_outgoing_msg(const MsgCloseGroup &msg) noexcept = 0;
    virtual void handle_outgoing_msg(const MsgGroupEmpty &msg) noexcept = 0;
    virtual void handle_outgoing_msg(const MsgAddToGroup &msg) noexcept = 0;

 


    void on_channels_update_lk() noexcept;


};


}
