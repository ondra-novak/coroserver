#include "peername.h"

#include <arpa/inet.h>
#include <stdexcept>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <cstring>
#include <system_error>

namespace coroserver {

const unsigned int PeerName::max_sockaddr_size = sizeof(sockaddr_storage);
const std::string_view PeerName::unix_prefix = "unix:";



struct PeerName::ToStringHlp {
    std::string operator()(const IPv4 &addr) const {
        int a = (addr.addr >> 24) & 0xFF;
        int b = (addr.addr >> 16) & 0xFF;
        int c = (addr.addr >> 8) & 0xFF;
        int d = (addr.addr /*>> 0*/) & 0xFF;
        int port = htons(addr.port);
        char buff[24];
        snprintf(buff,24,"%d.%d.%d.%d:%d",d,c,b,a,port);
        return buff;
    }
    std::string operator()(const IPv6 &addr) const {
        char buff[50];
        snprintf(buff, 50, "[%x:%x:%x:%x:%x:%x:%x:%x]:%d",
                htons(addr.addr[0]),
                htons(addr.addr[1]),
                htons(addr.addr[2]),
                htons(addr.addr[3]),
                htons(addr.addr[4]),
                htons(addr.addr[5]),
                htons(addr.addr[6]),
                htons(addr.addr[7]),
                htons(addr.port));
        return buff;
    }
    std::string operator()(const Unix &addr) const {
        return std::string(unix_prefix).append(addr.path);
    }
    std::string operator()(const Error &addr) const {
        try {
            std::rethrow_exception(addr.e);
        } catch (std::exception &e) {
            return std::string("Error: ").append(e.what());
        }

    }
    std::string operator()(const std::monostate &) const {
        return "<n/a>";

    }
};

std::string PeerName::to_string() const {
    return visit(ToStringHlp());
}

std::vector<PeerName> PeerName::lookup(std::initializer_list<std::string_view> names, std::string_view def_port) {
    std::vector<PeerName> list;
    for (const auto &c: names) {
        auto r = lookup(c, def_port);
        for (auto &x: r) {
            list.push_back(std::move(x));
        }
    }
    return list;
}

std::vector<PeerName> PeerName::lookup(std::string_view name, std::string_view def_port) {
    std::vector<PeerName> list;
    while (!name.empty()) {
        std::string_view item;
        auto sep = name.find(' ');
        if (sep == name.npos) {
            item = name;
            name = {};
        } else {
            item = name.substr(0, sep);
            name = name.substr(sep+1);
        }
        if (item.empty()) continue;

        try {

            if (item.compare(0,unix_prefix.size(), unix_prefix) == 0) {
                    resolve_local(item.substr(unix_prefix.size()),list);
                    continue;
            }

            std::string_view host;
            std::string_view port;

            if (item[0] == '[') {//ipv6
                auto spos = item.find(']');
                if (spos != item.npos) {
                    host = item.substr(1,spos-1);
                    spos = item.find(':', spos);
                    if (spos == item.npos) {
                        port = def_port;
                    } else {
                        port = item.substr(spos+1);
                    }
                }
            }
            if (host.empty()) {
                auto spos = item.rfind(':');
                if (spos == item.npos) {
                    port = def_port;
                    host = item;
                } else {
                    port = item.substr(spos+1);
                    host = item.substr(0,spos);
                }
            }

            if (port.empty()) {
                list.push_back(Error{std::make_exception_ptr(PortIsRequiredException())});
            } else {
                nslookup(std::string(host), std::string(port),list);
            }
        } catch (...) {
            list.push_back(Error{std::current_exception()});
        }
    }
    if (list.empty()) {
        throw std::invalid_argument("PeerName::lookup(\"\") can't be resolved");
    }
    for (const auto &x: list) if (x.valid()) return list;
    const Error *e = list[0].get_error() ;
    if (e) std::rethrow_exception(e->e);
    throw std::runtime_error("Not valid address returned");

}

void PeerName::nslookup(std::string &&host, std::string &&port, std::vector<PeerName> &list) {
    addrinfo hint = {};
    addrinfo *result;

    const char *hostptr = host.c_str();

    hint.ai_family = AF_UNSPEC;
    hint.ai_protocol = IPPROTO_TCP;
    if (host == "localhost" || host == "0") {
        hostptr = nullptr;
    }
    else if (host == "*" || host.empty())  {
        hostptr = nullptr;
        hint.ai_flags = AI_PASSIVE;
    }
    hint.ai_flags |= AI_ADDRCONFIG;

    const char *portptr = port.c_str();
    if (port.compare("*") == 0) {
        portptr = "0";
        hint.ai_flags = AI_PASSIVE;
    }


    int err = getaddrinfo(hostptr, portptr,&hint,&result);

    if (err) {
        throw LookupException(err);
    }

    addrinfo *iter = result;
    while (iter != nullptr) {
        PeerName p = PeerName::from_sockaddr(iter->ai_addr);
        if (p.valid()) list.push_back(std::move(p));
        iter = iter->ai_next;
    }

    freeaddrinfo(result);
}



unsigned int PeerName::to_sockaddr(void *sockaddr, unsigned int sockaddr_len) const {
    std::memset(sockaddr, 0, sockaddr_len);
    return visit([&](const auto &item) -> unsigned int {
        using Type = std::decay_t<decltype(item)>;
        if constexpr(std::is_same_v<Type, IPv4>) {
            constexpr unsigned int sz = sizeof(sockaddr_in);
            if (sockaddr_len < sz) return 0;
            auto *sin = reinterpret_cast<sockaddr_in *>(sockaddr);
            sin->sin_family = AF_INET;
            sin->sin_addr.s_addr = item.addr;
            sin->sin_port = item.port;
            return sz;
        } else if constexpr(std::is_same_v<Type, IPv6>) {
            constexpr unsigned int sz = sizeof(sockaddr_in6);
            if (sockaddr_len < sz) return 0;
            auto *saddr = reinterpret_cast<sockaddr_in6 *>(sockaddr);
            saddr->sin6_family = AF_INET6;
            std::copy(std::begin(item.addr), std::end(item.addr), std::begin(saddr->sin6_addr.__in6_u.__u6_addr16));
            saddr->sin6_flowinfo = item.flow_info;
            saddr->sin6_scope_id = item.scope_id;
            saddr->sin6_port = item.port;
            return sz;
        } else if constexpr(std::is_same_v<Type, Unix>) {
            constexpr unsigned int sz = sizeof(sockaddr_un);
            if (sockaddr_len < sz) return 0;
            auto *saddr = reinterpret_cast<sockaddr_un *>(sockaddr);
            saddr->sun_family = AF_UNIX;
            std::string p = item.path.string();
            if (p.size() >= sizeof(saddr->sun_path)) throw std::runtime_error("Path is too long");
            std::strncpy(saddr->sun_path, p.c_str(),sizeof(saddr->sun_path));
            return sz;
        } else if constexpr(std::is_same_v<Type, Error>) {
            std::rethrow_exception(item.e);
        } else {
            return 0;
        }
    });
}

PeerName PeerName::from_sockaddr(const void *s) {
    auto *saddr = reinterpret_cast<const sockaddr *>(s);
    switch (saddr->sa_family) {
        case AF_INET: {
            auto *addr = reinterpret_cast<const sockaddr_in *>(s);
            return IPv4{addr->sin_addr.s_addr, addr->sin_port};
        }
        case AF_INET6: {
            IPv6 r;
            auto *addr = reinterpret_cast<const sockaddr_in6 *>(s);
            std::copy(std::begin(addr->sin6_addr.__in6_u.__u6_addr16),
                      std::end(addr->sin6_addr.__in6_u.__u6_addr16),
                      std::begin(r.addr));
            r.flow_info = addr->sin6_flowinfo;
            r.scope_id = addr->sin6_scope_id;
            r.port = addr->sin6_port;
            return r;
        }
        case AF_UNIX: {
            auto *addr = reinterpret_cast<const sockaddr_un *>(s);
            return Unix{addr->sun_path, 0};
        }
        default:
            throw std::invalid_argument("Unsupported address type");

    }
}

struct PeerName::HasherHelper {
    std::size_t operator()(const IPv4 &addr) const {
       return (addr.addr<<16)+addr.port;
    }
    std::size_t operator()(const IPv6 &addr) const {
        std::string_view b(reinterpret_cast<const char *>(&addr),sizeof(addr));
        return std::hash<std::string_view>()(b);
    }
    std::size_t operator()(const Unix &addr) const {
        std::hash<std::string_view> h;
        return h(addr.path.string());
    }
    std::size_t operator()(const Error &) const {
        return 888;
    }
    std::size_t operator()(const std::monostate &) const {
        return -7777;

    }

};


std::size_t PeerName::hash() const {
    return visit(HasherHelper());
}

bool PeerName::equal_to(const PeerName &other) const {
    return _content == other._content;
}

bool PeerName::match(const PeerName &peer) const {
    return std::visit([](const auto &a, const auto &b)->bool{
        using A = std::decay_t<decltype(a)>;
        using B = std::decay_t<decltype(b)>;
        if constexpr(std::is_same_v<A,B>) {

            if constexpr(std::is_same_v<A, IPv4>) {
                return a.port == b.port &&
                        (a.addr == 0 || a.addr == b.addr);
            } else if constexpr(std::is_same_v<A, IPv6>) {
                bool allzeroes = std::all_of(std::begin(a.addr), std::end(a.addr), [](auto x){return x==0;});
                return (a.port == b.port && allzeroes) || a == b;

            } else {
                return a==b;
            }
        } else {
            return false;
        }
    },_content, peer._content);
}

void PeerName::resolve_local(std::string_view addr, std::vector<PeerName> &list) {
    auto splt = addr.rfind(':');
    int permission = 0;
    if (splt != addr.npos) {
        auto iter = addr.begin() + splt+1;
        auto end = addr.end();
        while (iter != end) {
            auto c = *iter;
            ++iter;
            switch (c) {
                case '0': permission = permission * 8;break;
                case '1': permission = permission * 8 + 1;break;
                case '2': permission = permission * 8 + 2;break;
                case '3': permission = permission * 8 + 3;break;
                case '4': permission = permission * 8 + 4;break;
                case '5': permission = permission * 8 + 5;break;
                case '6': permission = permission * 8 + 6;break;
                case '7': permission = permission * 8 + 7;break;
                case 'u': permission = permission | S_IRUSR | S_IWUSR; break;
                case 'g': permission = permission | S_IRGRP | S_IWGRP; break;
                case 'o': permission = permission | S_IROTH | S_IWOTH; break;
                default:
                    splt = addr.length();
                    iter = end;
                    break;
            }
        }
        addr = addr.substr(0, splt);
    }
    std::filesystem::path p(addr);
    p = std::filesystem::weakly_canonical(p);
    if (p.string().size() >= sizeof(sockaddr_un::sun_path)) {
        throw std::invalid_argument("Path to unix socket exceeded maximum 107 characters. Longer paths are not supported.");
    }
    list.push_back(PeerName(PeerName::Unix{std::move(p), permission}));

}


const char* PeerName::PortIsRequiredException::what() const noexcept {
    return "Port is required. Use <address>:<port> as peer name";
}

PeerName::LookupException::LookupException(int err):_err(err) {

}

const char* PeerName::LookupException::what() const noexcept {
    return gai_strerror(_err);
}

bool PeerName::IPv6::operator ==(const IPv6 &x) const {
    if (!std::equal(std::begin(addr), std::end(addr), std::begin(x.addr))) {
        return false;
    }
    return port == x.port;
}

bool PeerName::Unix::operator ==(const Unix &x) const {
    return path == x.path;
}

PeerName PeerName::from_socket(SocketHandle h, bool peer_name) {
    sockaddr_storage saddr;
    socklen_t slen = sizeof(saddr);
    auto fn = peer_name?&getpeername:&getsockname;
    if (fn(h,reinterpret_cast<sockaddr *>(&saddr), &slen) == -1) {
        int err = errno;
        throw std::system_error(err, std::system_category(), "getsockname");
    }
    return from_sockaddr(reinterpret_cast<sockaddr *>(&saddr));

}

std::string PeerName::get_port() {
    return std::visit([](const auto &x)->std::string {
        using T = std::decay_t<decltype(x)>;
        if constexpr(std::is_same_v<T, IPv4> || std::is_same_v<T, IPv6>) {
            return std::to_string(htons(x.port));
        } else {
            return std::string();
        }
    }, _content);
}

}
