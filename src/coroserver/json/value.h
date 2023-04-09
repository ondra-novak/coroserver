/*
 * value.h
 *
 *  Created on: 26. 1. 2023
 *      Author: ondra
 */

#ifndef SRC_USERVER_JSON_VALUE_H_
#define SRC_USERVER_JSON_VALUE_H_
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <variant>
#include <sstream>

namespace coroserver {

namespace json {

class Value;

struct Undefined {
    constexpr int operator<=>(const Undefined &) const {return 0;}
};


using Object = std::map<std::string, Value, std::less<> >;
using Array = std::vector<Value>;

class TextNumber : public std::string {
public:
    explicit TextNumber(std::string txt):std::string(std::move(txt)) {}
};

using PObject = std::shared_ptr<const Object>;
using PArray = std::shared_ptr<const Array>;

using ValueVariant = std::variant<
                Undefined,
                std::nullptr_t,
                bool,
                std::int32_t,
                std::uint32_t,
                std::int64_t,
                std::uint64_t,
                double,
                TextNumber,
                std::string,
                Object,
                Array,
                PObject,
                PArray>;

enum class Type {
    undefined,
    null,
    boolean,
    number,
    string,
    object,
    array

};

inline constexpr std::string_view str_true="true";
inline constexpr std::string_view str_false="false";
inline constexpr std::string_view str_undefined="undefined";
inline constexpr std::string_view str_null="null";
extern Value val_undefined;





class Value: public ValueVariant {
public:


    using ValueVariant::ValueVariant;

    struct KeyValue;

//    Value(const ValueVariant &other):ValueVariant(other) {}


    constexpr Type type() const {
        constexpr Type types[] = {
                Type::undefined,
                Type::null,
                Type::boolean,
                Type::number,
                Type::number,
                Type::number,
                Type::number,
                Type::number,
                Type::number,
                Type::string,
                Type::object,
                Type::array,
                Type::object,
                Type::array
        };
        return types[this->index()];
    }

    constexpr bool defined() const {return this->index() != 0;}
    constexpr bool has_value() const {return this->index() > 1;}
    constexpr bool is_null() const {return this->index() == 1;}

    constexpr double get_bool() const {
        return std::visit([](const auto &val) -> bool {
            using T = decltype(val);
            if constexpr(std::is_convertible_v<T, std::nullptr_t> || std::is_convertible_v<T, Undefined> ) {
                return false;
            }else if constexpr(std::is_convertible_v<T, bool>) {
                return val;
            } else if constexpr(std::is_convertible_v<T,double>) {
                return !!val;
            } else if constexpr(std::is_convertible_v<T,TextNumber>) {
                std::string_view s(val);
                auto v = std::strtod(s.data(),nullptr);
                return !!v;
            } else if constexpr(std::is_convertible_v<T,PObject> || std::is_convertible_v<T,PArray>) {
                return !val->empty();
            } else {
                return !val.empty();
            }
        }, *this);
    }

    template<typename X>
    constexpr X get_numeric() const {
        return std::visit([](const auto &val)->X{
            using T = decltype(val);
            if constexpr(std::is_convertible_v<T, std::nullptr_t>) {
                return 0;
            }else if constexpr(std::is_convertible_v<T, bool>) {
                return val?1:0;
            } else if constexpr(std::is_convertible_v<T,double>) {
                return static_cast<X>(val);
            } else if constexpr(std::is_convertible_v<T,std::string_view>) {
                std::string_view s(val);
                return static_cast<X>(std::strtod(s.data(),nullptr));
            } else {
                return 0.0;
            }
        }, *this);
    }

    constexpr auto get_double() const {return this->get_numeric<double>();}
    constexpr auto get_int() const {return this->get_numeric<int>();}
    constexpr auto get_uint() const {return this->get_numeric<uint>();}
    constexpr auto get_int32() const {return this->get_numeric<int32_t>();}
    constexpr auto get_uint32() const {return this->get_numeric<uint32_t>();}
    constexpr auto get_int64() const {return this->get_numeric<int64_t>();}
    constexpr auto get_uint64() const {return this->get_numeric<uint64_t>();}

    constexpr auto get_string_view() const {
        return std::visit([](const auto &val)->std::string_view{
            using T = decltype(val);
            if constexpr(std::is_convertible_v<T, Undefined>){
                return str_undefined;
            } else if constexpr(std::is_convertible_v<T,std::nullptr_t>) {
                return str_null;
            } else if constexpr(std::is_convertible_v<T,bool>) {
                return val?str_true:str_false;
            } else if constexpr(std::is_convertible_v<T, std::string_view>) {
                return std::string_view(val);
            } else {
                return std::string_view();
            }
        }, *this);
    }

    auto get_string() const {
        return std::visit([](const auto &val)->std::string{
            using T = decltype(val);
            if constexpr(std::is_convertible_v<T, std::string_view>) {
                return std::string(val);
            }else if constexpr(std::is_convertible_v<T, Undefined>){
                return std::string(str_undefined);
            } else if constexpr(std::is_convertible_v<T,std::nullptr_t>) {
                return std::string(str_null);
            } else if constexpr(std::is_convertible_v<T,bool>) {
                return std::string(val?str_true:str_false);
            } else if constexpr(std::is_convertible_v<T,double>) {
                std::ostringstream out;
                out << val;
                return out.str();
            } else {
                return std::string();
            }
        }, *this);
    }

    constexpr const Object &get_object() const {
        return std::visit([](const auto &val)->const Object & {
            using T = decltype(val);
            if constexpr(std::is_convertible_v<T, Object>) {
                return val;
            } else if constexpr(!std::is_convertible_v<T, std::nullptr_t> && std::is_convertible_v<T, PObject> ) {
                return *val;
            } else {
                static Object ret;
                return ret;
            }
        },*this);
    }
    constexpr const Array &get_array() const {
        return std::visit([](const auto &val)->const Array & {
            using T = decltype(val);
            if constexpr(std::is_convertible_v<T, Array>) {
                return val;
            } else if constexpr(!std::is_convertible_v<T, std::nullptr_t> && std::is_convertible_v<T, PArray>) {
                return *val;
            } else {
                static Array ret;
                return ret;
            }
        },*this);
    }

    Object &object() {
        return std::get<Object>(*this);
    }
    Array &array() {
        return std::get<Array>(*this);
    }

    constexpr const Value &operator[](std::size_t sz) const {
        if (std::holds_alternative<Array>(*this)) {
            const Array &a = std::get<Array>(*this);
            if (sz < a.size()) {
                return a[sz];
            }
        }
        return val_undefined;
    }
    const Value &operator[](std::string_view key) const {
        if (std::holds_alternative<Object>(*this)) {
            const Object &o = std::get<Object>(*this);
            Object::const_iterator iter = o.find(key);
            if (iter != o.end()) {
                return iter->second;
            }
        }
        return val_undefined;
    }
    Value &operator[](std::size_t sz) {
        if (std::holds_alternative<Array>(*this)) {
            Array &a = std::get<Array>(*this);
            if (sz < a.size()) {
                return a[sz];
            }
        }
        return val_undefined;
    }
    Value &operator[](std::string_view key) {
        if (std::holds_alternative<Object>(*this)) {
            Object &o = std::get<Object>(*this);
            Object::iterator iter = o.find(key);
            if (iter != o.end()) {
                return iter->second;
            }
        }
        return val_undefined;
    }

    std::string to_string() const;

    ///Makes value sharable
    /**
     * Sharable value can be shared between threads without any penalties, can
     * be copied without performing deep copy. However, the sharable content is
     * no longer editable. You can't use object() and array() to modify content.
     *
     * To convert back to editable, you need to call isolate(). Note that this
     * process performs deep copy
     *
     */
    void make_sharable() {
        if (std::holds_alternative<Object>(*this)) {
            *this = std::make_shared<const Object>(std::move(std::get<Object>(*this)));
        } else if (std::holds_alternative<Array>(*this)) {
            *this = std::make_shared<const Array>(std::move(std::get<Array>(*this)));
        }
    }

    ///Isolates sharable value
    /**
     * Replaces current value which is sharable with copy, which is editable - without
     * affecting original value
     */
    void isolate() {
        if (std::holds_alternative<PObject>(*this)) {
            *this = *std::get<PObject>(*this);
        } else if (std::holds_alternative<PArray>(*this)) {
            *this = *std::get<PArray>(*this);
        }
    }

    ///Determine whether content is sharable
    /**
     * @retval true content is sharable
     * @return false content is not sharable
     *
     * @note Only array and object can be in one of these two states. Other values
     * as treat as sharable because they are always copied, and thus can be shared
     * between threads
     */
    constexpr bool is_sharable() const {
        return !std::holds_alternative<Object>(*this) && !std::holds_alternative<Array>(*this);
    }

    Value(const std::initializer_list<Value> &v):Value(buildValue(v.begin(), v.end())) {}

    template<typename Iter>
    static Value buildValue(Iter begin, Iter end);

/*    operator bool() const {
        return std::visit([this](const auto &v)->bool{
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, TextNumber>) {
                return std::strtod(v.data(),nullptr) != 0;
            } else if constexpr (std::is_same_v<T, Array> || std::is_same_v<T, Object> || std::is_same_v<T, std::string>) {
                return !v.empty();
            } else if constexpr (std::is_null_pointer_v<T> || std::is_same_v<T, Undefined>) {
                return false;
            }else {
                return (bool)v;
            }
        }, *this);
    }*/

};


template<typename Iter>
Value Value::buildValue(Iter begin, Iter end) {
    if (std::all_of(begin, end,[&](const Value &v){
        if (std::holds_alternative<Array>(v)) {
            const Array &a = std::get<Array>(v);
            if (a.size() == 2 && std::holds_alternative<std::string>(a[0])) {
                return true;
            }
        }
        return false;
    })) {
        Object r;
        for (Iter iter = begin; iter != end; ++iter) {
            const Array &a = std::get<Array>(*iter);
            r.emplace(std::get<std::string>(a[0]), a[1]);
        }
        return Value(std::move(r));
    }
    else {
        return Value(Array(begin, end));

    }
}

/*
inline Value make(std::initializer_list<Value> init_list) {
    bool is_object = std::all_of(init_list.begin(), init_list.end(), [](const Value &v){
        return std::holds_alternative(__v)
    })
}
*/


inline Value val_undefined;

Value from_string(std::string_view s);





}

}



#endif /* SRC_USERVER_JSON_VALUE_H_ */
