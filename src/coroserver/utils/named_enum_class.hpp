#pragma once

#include "named_enum.hpp"


#define DECLARE_ENUM_CLASS(name,...) class name {\
    public:\
        enum _ : std::uint8_t {\
            __VA_ARGS__\
        };\
        constexpr name() = default;\
        constexpr name(_ value):_value(value) {}\
        \
        _ value() const {return _value;} \
        constexpr std::string_view to_string() const {return strings[_value];} \
        constexpr auto begin() const {return strings.begin();}\
        constexpr auto end() const {return strings.end();}\
        constexpr static name from_string(std::string_view s) {return name(strings[s]);}\
        bool operator==(const name &other) const = default; \
        template<typename T> \
        requires(std::is_integral_v<T>) \
        explicit operator T() const {return static_cast<T>(_value);} \
protected:\
        static constexpr auto strings = makeEnumToStringLookupTableHelper<_>([]{ \
                return #__VA_ARGS__; \
        }); \
        _ _value={};\
}\



