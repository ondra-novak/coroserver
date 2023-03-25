/*
 * peername.h
 *
 *  Created on: 25. 3. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_PEERNAME_H_
#define SRC_COROSERVER_PEERNAME_H_
#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>


struct sockaddr;

namespace coroserver {


///Contans network address which can be used to listen or connect oder peer
class PeerName {
public:
    static const unsigned int max_sockaddr_size;
    static const std::string_view unix_prefix;


    struct IPv4 {
        std::uint32_t addr;
        std::uint16_t port;
        bool operator == (const IPv4 &x) const {
            return addr == x.addr && port == x.port;
        }
    };

    struct IPv6 {
        std::uint16_t addr[8];
        std::uint16_t port;
        std::uint32_t flow_info;
        std::uint32_t scope_id;
        bool operator == (const IPv6 &x) const;
    };

    struct Unix {
        std::filesystem::path path;
        int perms;
        bool operator == (const Unix &x) const;
    };

    struct Error {
        std::exception_ptr e;
        bool operator == (const Error &) const {
            return false;
        }
    };

    ///Construct default peer name, which has no value
    /** Content of variable doesn't refer any peer, and can't be used */
    PeerName() = default;
    ///Construct IPv4 address
    PeerName(IPv4 addr):_content(std::move(addr)) {}
    ///Construct IPv6 address
    PeerName(IPv6 addr):_content(std::move(addr)) {}
    ///Construct Unix socket address
    PeerName(Unix addr):_content(std::move(addr)) {}
    ///Construct error address
    PeerName(Error addr):_content(std::move(addr)) {}

    ///Convert address to string
    std::string to_string() const;
    ///Perform address lookup
    /**
     * @param name requested peer name. You can request multiple peers, where each peer is separated by space.
     * @param def_port default port, when there is no port specification. If this parameter is not set, all peers must have a port
     * @return list of found peers. If no peer is found, exception is thrown, so the result contains always at least one peer
     */
    static std::vector<PeerName> lookup(std::string_view name, std::string_view def_port = {});


    ///constructs sockaddr from peer, to perform low-level network access
    /**
     * @param sockaddr pointer to buffer where sockaddr will be constructed
     * @param sockaddr_len size of the buffer
     * @return returns count of bytes written to the buffer, return 0, if operation cannot be performed
     */
    unsigned int to_sockaddr(void *sockaddr, unsigned int sockaddr_len) const;

    ///constructs PeerName from sockaddr
    /**
     * @param sockaddr pointer to sockaddr
     * @return constructed objecet
     */
    static PeerName from_sockaddr(const void *sockaddr);

    ///Visit the content
    /**
     * @param fn function, it can receive any argument depend on type of address
     * @return return value of function
     */
    template<typename Fn>
    auto visit(Fn &&fn) const {
        return std::visit(std::forward<Fn>(fn), _content);
    }

    ///Calls function with sockaddr * and socklen passed as arguments
    /**
     * @param fn function to call (sockaddr *, socklen)
     * @return return value of function
     */
    template<typename Fn>
    auto use_sockaddr(Fn &&fn) {
        unsigned int sz = max_sockaddr_size;
        void *buff = alloca(max_sockaddr_size);
        sz = to_sockaddr(buff, sz);
        return fn(reinterpret_cast<const sockaddr *>(buff), sz);
    }

    ///Captures sockaddr and receives PeerName
    /**
     * @param fn function to be called, it receives pointer to sockaddr and size of the reserved space.
     * Function must returns count of written bytes, or zero if error
     * @return PeerName if nonzero bytes has been written, or empty PeerName if error.
     */
    template<typename Fn>
    static PeerName capture_sockaddr(Fn &&fn) {
        unsigned int sz = max_sockaddr_size;
        void *buff = alloca(max_sockaddr_size);
        unsigned int capsz = fn(buff, sz);
        if (capsz == 0) return PeerName();
        else return from_sockaddr(buff);
    }

    ///Determines, whether peer name is valid
    /**
     * @retval true is valid
     * @retval false variable doesn't contains value, or contains error
     */
    bool valid() const {
        return !std::holds_alternative<std::monostate>(_content) &&
                !std::holds_alternative<Error>(_content);
    }

    ///Retrieve IPv4 informations
    /**
     * @return pointer to IPv4 struct, or nullptr if content is not IPv4
     */
    const IPv4 *get_ipv4() const {
        if (std::holds_alternative<IPv4>(_content)) return &std::get<IPv4>(_content);
        else return nullptr;
    }
    ///Retrieve IPv6 informations
    /**
     * @return pointer to IPv6 struct, or nullptr if content is not IPv6
     */
    const IPv6 *get_ipv6() const {
        if (std::holds_alternative<IPv6>(_content)) return &std::get<IPv6>(_content);
        else return nullptr;
    }
    ///Retrieve UnixSocket informations
    /**
     * @return pointer to UNix struct, or nullptr if content is not Unix
     */
    const Unix *get_unix() const {
        if (std::holds_alternative<Unix>(_content)) return &std::get<Unix>(_content);
        else return nullptr;
    }
    ///Retrieve Error informations
    const Error *get_error() const {
        if (std::holds_alternative<Error>(_content)) return &std::get<Error>(_content);
        else return nullptr;
    }

    ///Type of stored information
    enum class Type {
        invalid = 0,
        ipv4 = 1,
        ipv6 = 2,
        unix_socket = 3,
        error = 4
    };

    ///Get type of peer name
    Type getType() {return static_cast<Type>(_content.index());}

    ///Retrieve hash for hashing for unordered map
    std::size_t hash() const;
    ///Compare with other address
    bool equal_to(const PeerName &other) const;

    ///Exception
    class PortIsRequiredException: public std::exception {
    public:
        virtual const char *what() const noexcept override;
    };

    ///Exception
    class LookupException: public std::exception {
    public:
        LookupException(int err);
        const char *what() const noexcept override;
        int code() const {return _err;}
    protected:
        int _err;

    };

    bool operator==(const PeerName &x) const {
        return equal_to(x);
    }
protected:

    std::variant<std::monostate,IPv4, IPv6, Unix, Error> _content;

    static void nslookup(std::string &&host, std::string &&port, std::vector<PeerName> &list);
    static void resolve_local(std::string_view path, std::vector<PeerName> &list);

    struct ToStringHlp;
    struct HasherHelper;
};




}




#endif /* SRC_COROSERVER_PEERNAME_H_ */
