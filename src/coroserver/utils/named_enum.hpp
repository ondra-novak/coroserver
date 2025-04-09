#pragma once

#include "static_lookup_table.hpp"

namespace _named_enum_details {

///Parses content of enum {...} stored in string to correctly register all enum values
/**
 * @tparam UnderlyingEnumType - underlying enum type
 * @param iter begin iterator (char)
 * @param end end iterator (char)
 * @param fn function which is called with every key-value pair, contains ("string", UnderlyingEnumType)
 */
template<typename UnderlyingEnumType, typename Iter, typename Fn>
inline constexpr void enumSyntaxParser(Iter iter, Iter end, Fn fn) {
    //the parser is very simple, it doesn't do any validation, it is expected, that compiler handles validation
    //so it expects comma separated list
    //invalid / unexpected characters are ignored
    //character '=' separes identifier and the value
    // [0-9]+ for decimal number
    // 0[0-7]+ for octal number
    // 0x[0-9a-fA-F]+ for hexadecimal number

    char collect[1024]; //GCC-11 doesn't support std::string. Yep, this limits the size of an identifier up to 1024 characters
    int collect_pos = 0;
    UnderlyingEnumType idx = 0;
    UnderlyingEnumType newidx;
    bool newidx_set = false;
    enum State {ident,  number, decimal, octal, hex};
    State st = ident;
    while (iter != end) {
        if (*iter == ',') {
            if (collect_pos) {
                if (newidx_set) idx = newidx;
                fn(std::string_view(collect, collect_pos), idx);
                ++idx;
            }
            collect_pos =0;
            newidx_set = false;
            st = ident;
            ++iter;
            continue;
        }

        switch (st) {
            case ident: if ((*iter>='0' && *iter <='9') || (*iter == '_') || (*iter >= 'a' && *iter <= 'z') || (*iter >= 'A' && *iter <= 'Z')) {
                            collect[collect_pos++] = *iter;
                        } else  if (*iter == '=') {
                            st = number;
                        }
                break;
            case number: if (*iter == '0') {
                            st = octal;
                            newidx = 0;
                            newidx_set = true;
                        } else if (*iter > '0' && *iter <= '9') {
                            st = decimal;
                            newidx = (*iter - '0');
                            newidx_set = true;
                        }
                break;
            case decimal: if (*iter >= '0' && *iter <= '9') {
                            newidx = newidx * 10 + (*iter - '0');
                          }
                break;
            case octal:
                    if (*iter>='0' && *iter < '8') {
                        newidx = newidx * 8 + (*iter - '0');
                        newidx_set = true;
                    } else if (*iter == 'x') {
                        st = hex;
                    }
                break;
            case hex:
                    if (*iter>='0' && *iter <= '9') {
                        newidx = newidx * 16 + (*iter - '0');
                    } else if (*iter>='a' && *iter <= 'f') {
                        newidx = newidx * 16 + (*iter - 'a' + 10);
                    } else if (*iter>='A' && *iter <= 'F') {
                        newidx = newidx * 16 + (*iter - 'A' + 10);
                    }
                break;
        }
        ++iter;
    }
    if (collect_pos) {
        if (newidx_set) idx = newidx;
        fn(std::string_view(collect, collect_pos), idx);
    }
}

/// Calculates count of items from string
/** @tparam EnumType enum type
 * @tparam string_fn - lambda function, which returns stringified enum declaration - need to enforce constexpr of the string
 */
template<typename EnumType, typename StringSource>
inline constexpr std::size_t enumCountItems = ([]() {
    StringSource fn;
    std::size_t count = 0;
    std::string_view text = fn();
    enumSyntaxParser<std::underlying_type_t<EnumType> >(text.begin(), text.end(),[&](auto, auto) { ++count; });
    return count;
})();

}


///Class which implements NAMED_ENUM macro
template<typename EnumType, typename StringSource>
class EnumToStringLookupTable: public StaticLookupTable<EnumType, std::string_view, _named_enum_details::enumCountItems<EnumType, StringSource> > {

public:
    using Super = StaticLookupTable<EnumType, std::string_view, _named_enum_details::enumCountItems<EnumType, StringSource> >;
    using EnumUnderlyingType = typename Super::EnumUnderlyingType;
    static constexpr StringSource string_fn = {};


    static constexpr int string_area_size = ([]{
        int count = 0;
        std::string_view text = string_fn();
        _named_enum_details::enumSyntaxParser<std::underlying_type_t<EnumType> >(text.begin(), text.end(),[&](auto a, auto) { count += a.size()+1; });
        return count;
    })();

    constexpr EnumToStringLookupTable() {
        init_content();
    }

    constexpr const char *get_string_area() const {return _string_area;}

protected:
    char _string_area[string_area_size];

    constexpr void init_content() {
        int strpos = 0;
        int tblpos = 0;
        std::string_view text = string_fn();
        _named_enum_details::enumSyntaxParser<std::underlying_type_t<EnumType> >(text.begin(), text.end(), [&](std::string_view text, EnumUnderlyingType idx) {
            std::copy(text.begin(), text.end(), _string_area+strpos);
            std::string_view tref(_string_area+strpos, text.size());
            strpos+=text.size();
            _string_area[strpos] = 0;
            ++strpos;

            std::construct_at(&Super::_items[tblpos].x, typename Super::Item{
                static_cast<EnumType>(idx), tref
            });
            ++tblpos;
        });
        std::sort(std::begin(Super::_items), std::end(Super::_items), [](const auto &a, const auto &b) {
            return a->key < b->key;
        });
        Super::initIndex();
    }

};

template<typename EnumType, std::invocable<> StringSource>
constexpr EnumToStringLookupTable<EnumType, StringSource> makeEnumToStringLookupTableHelper(StringSource) {
    return {};
}

/**
 * This macro declares a standard enum type.
 * The first parameter of the macro specifies the name of this type,
 * and the following parameters specify the individual values of the type,
 * as they would normally be listed in the enum declaration.
 * The enum is always declared with the keyword 'class'.
 * Each enum value can also specify a numerical value after the '=' operator,
 * as is common in this declaration. There is only the following limitation:
 * the numerical constant must be represented as a number.
 * It is not allowed to use an expression, even if the expression can
 * be evaluated during compilation. The number can be specified in decimal,
 * octal, or hexadecimal notation, with the appropriate prefix.
 *
 * @code
 * NAMED_ENUM(Color,
 *     blue,
 *     green,
 *     red,
 *     yellow
 * )
 *
 * NAMED_ENUM(NType,
 *      normal,
 *      decimal = 1,
 *      octal = 0657,
 *      hexadecimal = 0xABC123
 * )
 * @endcode
 *
 *
 * Along with the introduction of the given type, a declaration of
 * the class NamedEnum_<Typename> is created, where the <Typename>
 * is replaced with the name of this type. This class provides
 * conversion of the value to its string representation and back.
 * To use the class, it is necessary to construct an instance as
 * a variable, and then perform conversions using this variable.
 */
#define NAMED_ENUM(Typename, ...) enum class Typename { __VA_ARGS__}; \
using NamedEnum_##Typename =  decltype(makeEnumToStringLookupTableHelper<Typename>([]{return #__VA_ARGS__;}))



