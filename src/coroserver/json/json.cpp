
#include "parser.h"
#include "serializer.h"

#include <cocls/alloca_storage.h>

namespace coroserver {

struct StrWrite {
    std::string &s;

    cocls::future<bool> write(std::string_view x) {
        s.append(x);
        return cocls::future<bool>::set_value(true);
    }
};


std::string json::Value::to_string() const {
    std::string out;
    static std::size_t allocsz = 0;
    cocls::stack_storage stor(allocsz);
    stor=alloca(stor);
    serialize_coro<cocls::stack_storage, StrWrite>(stor, {out}, *this).join();
    return out;
}

struct StrRead {
    std::string_view _buff;
    void put_back(std::string_view buff) {_buff = buff;}
    cocls::future<std::string_view> read() {
        return cocls::future<std::string_view>::set_value(std::exchange(_buff, {}));
    }

};

json::Value json::from_string(std::string_view s) {
    static std::size_t allocsz = 0;
    cocls::stack_storage stor(allocsz);
    stor=alloca(stor);
    return parse_coro<cocls::stack_storage, StrRead>(stor, {s}).join();
}

}
