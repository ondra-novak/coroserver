
#include "parser.h"
#include "serializer.h"

#include <coro.h>

namespace coroserver {

struct StrWrite {
    std::string &s;

    coro::future<bool> write(std::string_view x) {
        s.append(x);
        return coro::future<bool>::set_value(true);
    }
};


std::string json::Value::to_string() const {
    return Serializer(*this).to_string();
}

struct StrRead {
    std::string_view _buff;
    void put_back(std::string_view buff) {_buff = buff;}
    coro::future<std::string_view> read() {
        return coro::future<std::string_view>::set_value(std::exchange(_buff, {}));
    }

};

json::Value json::from_string(std::string_view s) {
    static std::size_t allocsz = 0;
    coro::stack_storage stor(allocsz);
    stor=alloca(stor);
    return parse_coro<coro::stack_storage, StrRead>(stor, {s}).join();
}

}
