#pragma once
#include <chrono>

namespace coroserver {

class TimeoutSpec {
public:
    template<typename A, typename B>
    constexpr TimeoutSpec(std::chrono::duration<A,B> dur):_duration(dur) {}
    constexpr TimeoutSpec(std::size_t duration_ms):_duration(std::chrono::milliseconds(duration_ms)) {}

    TimeoutSpec():_duration(std::chrono::system_clock::duration::max()) {}

    std::chrono::system_clock::time_point get_time_point() const {
        if (is_active())
            return std::chrono::system_clock::now()+_duration;
        else
            return std::chrono::system_clock::time_point::max();
    }

    constexpr std::chrono::system_clock::duration get_duration() const {
        return _duration;
    }

    operator std::chrono::system_clock::time_point() const {
        return get_time_point();
    }

    template<typename A, typename B>
    constexpr operator std::chrono::duration<A,B>() const {
        return std::chrono::duration_cast<std::chrono::duration<A,B> >(_duration);
    }

    constexpr bool is_active() const {
        return _duration != std::chrono::system_clock::time_point::duration::max();
    }

    constexpr explicit operator bool() const {
        return is_active();
    }

    bool operator==(const TimeoutSpec&) const = default;

protected:
    std::chrono::system_clock::duration _duration;
};


struct IOTimeout {
    TimeoutSpec _receive;
    TimeoutSpec _send;
};

constexpr IOTimeout default_io_timeout = {30000, 30000};



}
