#pragma once
#ifndef SRC_COROSERVER_TIMEOUT_H_
#define SRC_COROSERVER_TIMEOUT_H_

namespace coroserver {

class TimeoutSettings {



public:
    using Dur = std::chrono::system_clock::duration;
    using Tp = std::chrono::system_clock::time_point;

    constexpr TimeoutSettings() = default;

    template<typename A1, typename B1, typename A2, typename B2, typename A3, typename B3>
    constexpr TimeoutSettings(std::chrono::duration<A1, B1> read_timeout,
                    std::chrono::duration<A2, B2> write_timeout,
                    std::chrono::duration<A2, B2> total_timeout)
       :read(std::chrono::duration_cast<Dur>(read_timeout))
       ,write(std::chrono::duration_cast<Dur>(write_timeout))
       ,expiration(std::chrono::system_clock::now()+total_timeout) {}

    template<typename A1, typename B1, typename A2, typename B2>
    constexpr TimeoutSettings(std::chrono::duration<A1, B1> read_timeout,
                    std::chrono::duration<A2, B2> write_timeout)
       :read(std::chrono::duration_cast<Dur>(read_timeout))
       ,write(std::chrono::duration_cast<Dur>(write_timeout))
       {}

    template<typename A1, typename B1>
    constexpr TimeoutSettings(std::chrono::duration<A1, B1> timeout)
       :read(std::chrono::duration_cast<Dur>(timeout))
       ,write(std::chrono::duration_cast<Dur>(timeout))
       {}

    template<typename A1, typename B1>
    static constexpr Tp from_duration(std::chrono::duration<A1, B1> timeout) {
        return timeout == timeout.max()?Tp::max():std::chrono::system_clock::now()+timeout;
    }

    constexpr Tp get_read_timeout() {
        auto tp = read == Dur::max()?Tp::max():std::chrono::system_clock::now()+read;
        return std::min(tp, expiration);
    }
    constexpr Tp get_write_timeout() {
        auto tp = write == Dur::max()?Tp::max():std::chrono::system_clock::now()+read;
        return std::min(tp, expiration);
    }
    template<typename A1, typename B1>
    constexpr void set_expiration(std::chrono::duration<A1, B1> timeout) {
        std::chrono::system_clock::now()+std::chrono::milliseconds(timeout);
    }
    template<typename A1, typename B1>
    constexpr void set_read_timeout(std::chrono::duration<A1, B1> timeout) {
        read = std::chrono::duration<Dur>(timeout);
    }
    template<typename A1, typename B1>
    constexpr void set_write_timeout(std::chrono::duration<A1, B1> timeout) {
        write = std::chrono::duration<Dur>(timeout);
    }



protected:
    ///read timeout in milliseconds
        Dur read = Dur::max();
        ///write timeout in milliseconds
        Dur write = Dur::max();
        ///specifies timepoint, when IO operations expires for good.
        /**
         * This can be useful to mitigate some DoS attacts, such a Slowloris attach. You
         * can specify a timeout in near future, when IO operation always timeouts, so you
         * can't wait for IO operation after that point. This probably leads to closing the connection
         * Default value is infinity time.
         */
        Tp expiration = Tp::max();

};

constexpr TimeoutSettings defaultTimeout = {std::chrono::seconds(60)};
constexpr TimeoutSettings::Dur defaultConnectTimeout = std::chrono::seconds(60);


}




#endif /* SRC_COROSERVER_TIMEOUT_H_ */
