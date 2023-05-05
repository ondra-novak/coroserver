#ifndef SRC_COROSERVER_STATIC_LOOKUP_H_
#define SRC_COROSERVER_STATIC_LOOKUP_H_


#include <algorithm>
#include <optional>
#include <iterator>
#include <cocls/common.h>

namespace coroserver {

namespace _detail {
    template<typename T>
    concept Comparable = requires(T a, T b) {
        { a < b } -> std::same_as<bool>;
        { a <= b } -> std::same_as<bool>;
        { a > b } -> std::same_as<bool>;
        { a >= b } -> std::same_as<bool>;
    };
}

///Construct mapping table for enum to specified value type
/**
 * @tparam EnumType type of enum to be mapped. Can be any enum, however it is also possible to use any integral type.
 * @tparam ValueType type of value mapped to specified enum value
 * @tparam Count Count of items
 *
 * @note for convience, you can use makeStaticLookupTable<EnumType,ValueType>({...}) which also fills correct count
 * of items.
*/
template<typename EnumType, typename ValueType, int Count>
class StaticLookupTable {
public:

    ///Format of key-value item
    /**
     * Defines format of definition {
     *   {<enum>,<value>},
     *   {<enum>,<value>},
     *   ...
     * }
    */
    struct Item {
        ///enum type - key
        EnumType key;
        ///value type - value
        ValueType value;
    };

    ///contains underlying enum type, for non-enum type, contains EnumType
    using EnumUnderlyingType = typename std::conditional_t<std::is_enum_v<EnumType>,std::underlying_type<std::decay_t<EnumType> >, std::decay<EnumType> >::type;
    ///Contains true, if ValueType can be ordered. For large set of items it is better to support ordering, otherwise fullrow scan is used.
    #if defined( __cpp_concepts) and not defined (__CDT_PARSER__)
    constexpr static bool is_ordered =  _detail::Comparable<ValueType>;
    #else
    constexpr static bool is_ordered =  false;
    #endif
    constexpr static int log2(int x) {
        int c = 0;
        while (x) {++c; x>>=1;}
        return c;
    }

    constexpr static bool can_be_sequence =std::is_integral_v<EnumType> || std::is_enum_v<EnumType>;

    constexpr static int search_cycles = log2(Count);

    ///Declaration of storage
    /**
     * Main reason for this structure is to allow types without default constructor.
     * We need unitialized array of items before the constructor body is executed
     * The constructor does initialization fer item
     */
    union ItemStorage {
        Item x;
        constexpr ItemStorage() {}
        constexpr ~ItemStorage() {}
        constexpr const Item *operator->() const {return &x;};
    };

    ///Contains count of items
    constexpr static int count = Count;

    ///helper class
    struct ValueIndexArray {
        int pos[Count] = {};
    };

    ///helper class
    struct ValueIndexNone {};

    ///Contains type of index structure
    using ValueIndex = std::conditional_t<is_ordered,ValueIndexArray,ValueIndexNone>;

    ///Constructs object from an array of items
    /**
     * @param items array of items. Count of items must be exact as declared by Count variable. However
     * for convience you can use makeStaticLookupTable()
    */
    template<std::size_t c>
    CXX20_REQUIRES((c == Count))
    constexpr StaticLookupTable(const Item (&items)[c]) {
        int order[Count];
        for (int i = 0; i < Count; i++) {
            order[i] = i;
        }
        std::sort(std::begin(order), std::end(order), [&](int a, int b) {
            const Item &ia = items[a];
            const Item &ib = items[b];
            if (ia.key == ib.key) return a<b;
            else return ia.key < ib.key;
        });
        for (int i = 0; i < Count; i++) {
            std::construct_at(&_items[i].x, items[order[i]]);
        }
        initIndex();
    }

    ///Destructs the object
    /** Need to non-constexpr object work correctly */
    constexpr ~StaticLookupTable() {
        for (auto &x: _items) {
            x.x.~Item();
        }
    }

    ///Contains default value if enum is not registered in the table, you can redefine this in specialization
    static constexpr ValueType defaultValue = {};
    ///Contains default enum if value is not found in the table, you can redefine this in specialization
    static constexpr EnumType defaultEnum = {};

    /// Get value registered for given enum value
    /**
     * @param evalue enum value
     * @param defval default value in case when enum is not registered
     * @return found value or default value
     *
     * @note if the underlying value of each registered enum is sequence of numbers 1,2,3,4,5,6, the lookup
     * has O(1) complexity, otherwise it has O(log n) complexity
    */
    constexpr const ValueType &get(const EnumType &evalue, const ValueType &defval = defaultValue) const {
        auto iter = find(evalue);
        if (iter == end()) return defval;
        else return iter->value;
    }

    /// Get enum value registered for given value
    /**
     * @param v value
     * @param defval default value
     * @return found enum or default value
     *
     * @note if the value type is ordered, the lookup has O(log n) complexity otherwise it has O(n) complexity
    */
    constexpr const EnumType &get(const ValueType &v, const EnumType &defval = defaultEnum) const {
        auto iter = find(v);
        if (iter == end()) return defval;
        else return iter->key;
    }

    ///@see get();
    constexpr const ValueType &operator[](const EnumType &evalue) const {return get(evalue);}
    ///@see get();
    constexpr const EnumType &operator[](const ValueType &v) const {return get(v);}


    ///return count of items
    static constexpr int size() {return Count;}

    ///iterator
    class Iterator {
    public:
        typedef std::ptrdiff_t difference_type;
        typedef Item value_type;
        typedef const Item* pointer;
        typedef const Item& reference;
        typedef std::random_access_iterator_tag iterator_category;

        constexpr Iterator(const ItemStorage *ptr):_ptr(ptr) {}
        constexpr reference operator *() const {return _ptr->x;}
        constexpr pointer operator->() const {return &_ptr->x;}
        Iterator &operator++() {++_ptr; return *this;}
        Iterator &operator--() {--_ptr; return *this;}
        Iterator &operator+=(ptrdiff_t x) {_ptr+=x; return *this;}
        Iterator &operator-=(ptrdiff_t x) {_ptr-=x; return *this;}
        Iterator operator++(int) {auto me = *this; ++_ptr; return me;}
        Iterator operator--(int) {auto me = *this; --_ptr; return me;}
        Iterator operator+(ptrdiff_t x) {return Iterator(_ptr+x);}
        Iterator operator-(ptrdiff_t x) {return Iterator(_ptr-x);}
        bool operator==(const Iterator &other) const {return _ptr == other._ptr;}
    protected:
        const ItemStorage *_ptr;

    };

    ///returns find enum
    constexpr Iterator begin() const {return Iterator(_items);}
    ///returns last enum
    constexpr Iterator end() const {return Iterator(_items+Count);}

    constexpr auto rbegin() const {return std::make_reverse_iterator<Iterator>(end());}
    constexpr auto rend() const {return std::make_reverse_iterator<Iterator>(begin());}

    ///finds record for given enum value
    /**
     * @param evalue value to find
     * @return returns iterator or end() if not found
    */
    constexpr Iterator find(const EnumType &evalue) const {
        if constexpr(can_be_sequence) {
            if (_sequence) {
                if (evalue >= _items[0]->key && evalue <= _items[Count-1]->key) {
                    int offset = static_cast<int>(static_cast<EnumUnderlyingType>(evalue) - static_cast<EnumUnderlyingType>(_items[0]->key));
                    return Iterator(_items+offset);
                }
                return end();
            }
        }
        return Iterator(_items+lower_bound([&](int idx){return _items[idx]->key;}, evalue));
    }

    ///finds record for given  value
    /**
     * @param evalue v to find
     * @return returns iterator or end() if not found
    */
    constexpr Iterator find(const ValueType &v) const {
        if constexpr(is_ordered) {
            int pos = lower_bound([&](int idx){return _items[_valueIndex.pos[idx]]->value;}, v);
            if (pos == Count) return Iterator(_items+Count);
            else return Iterator(_items+_valueIndex.pos[pos]);
        } else {
            int it = 0;
            while (it < Count && _items[it]->value != v) ++it;
            return Iterator(it);
        }
    }

protected:
    //storage for all items
    ItemStorage _items[Count] = {};
    //contains index for search items by value.
    ValueIndex _valueIndex;
    //contains true, if registered enum values are sequence of numbers 1,2,3,4,5,6 so index lookup can be used
    bool _sequence = false;

    StaticLookupTable() = default;

    constexpr void initIndex() {
        _sequence = is_sequence();
        if constexpr(is_ordered) {
            for (int i = 0; i < Count; i++) {
                _valueIndex.pos[i] = i;
            }
            std::sort(std::begin(_valueIndex.pos),std::end(_valueIndex.pos), [&](int a, int b){
                return _items[a]->value < _items[b]->value;
            });
        }
    }
    ///
    constexpr bool is_sequence() const {
        if constexpr(can_be_sequence) {
            EnumType itr = _items[0]->key;
            for (int i = 1; i < Count; i++) {
                if constexpr(std::is_enum_v<EnumType>) {
                    itr = static_cast<EnumType>(static_cast<EnumUnderlyingType>(itr)+1);
                } else {
                    ++itr;
                }
                if (_items[i]->key != itr) return false;
            }
            return true;
        } else {
            return false;
        }
    }

    template<typename Field, typename Value>
    constexpr int lower_bound(Field &&field, Value &&value) const {
        int first = 0;
        int count = Count;
        for (int i = 0; i < search_cycles; i++) {
            auto step = count / 2;
            auto it = first + step;
            int res[4] = {first, step, it+1, count - step-1};
            int sel = 2 * (field(it) < value);
            first = res[sel];
            count = res[sel+1];
        }
        int r1 = (first >= Count);
        int r2 = field(first-r1) != value;
        int outs[] = {first, Count};
        return outs[r1 | r2];

    }

};

///Create NamedEnum instance
/**
 * @tparam EnumType type of enum value
 * @tparam ValueType type of associated value
 * @param x array of items key-value pairs - {enum, value}
*/
template<typename EnumType, typename ValueType, int N>
inline constexpr auto makeStaticLookupTable(const typename StaticLookupTable<EnumType, ValueType, N>::Item (&x)[N]) {
    return StaticLookupTable<EnumType,ValueType, N>(x);
}

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
    std::optional<UnderlyingEnumType> newidx;
    enum State {ident,  number, decimal, octal, hex};
    State st = ident;
    while (iter != end) {
        if (*iter == ',') {
            if (collect_pos) {
                if (newidx.has_value()) idx = *newidx;
                fn(std::string_view(collect, collect_pos), idx);
                ++idx;
            }
            collect_pos =0;
            newidx.reset();
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
                        } else if (*iter > '0' && *iter <= '9') {
                            st = decimal;
                            newidx = (*iter - '0');
                        }
                break;
            case decimal: if (*iter >= '0' && *iter <= '9') {
                            newidx = *newidx * 10 + (*iter - '0');
                          }
                break;
            case octal:
                    if (*iter>='0' && *iter < '8') {
                        newidx = *newidx * 8 + (*iter - '0');
                    } else if (*iter == 'x') {
                        st = hex;
                    }
                break;
            case hex:
                    if (*iter>='0' && *iter <= '9') {
                        newidx = *newidx * 16 + (*iter - '0');
                    } else if (*iter>='a' && *iter <= 'f') {
                        newidx = *newidx * 16 + (*iter - 'a' + 10);
                    } else if (*iter>='A' && *iter <= 'F') {
                        newidx = *newidx * 16 + (*iter - 'A' + 10);
                    }
                break;
        }
        ++iter;
    }
    if (collect_pos) {
        if (newidx.has_value()) idx = *newidx;
        fn(std::string_view(collect, collect_pos), idx);
    }
}

/// Calculates count of items from string
/** @tparam EnumType enum type
 * @tparam string_fn - lambda function, which returns stringified enum declaration - need to enforce constexpr of the string
 */
template<typename EnumType, auto string_fn>
inline constexpr int enumCountItems = ([]() {
    int count = 0;
    std::string_view text = string_fn();
    enumSyntaxParser<std::underlying_type_t<EnumType> >(text.begin(), text.end(),[&](auto, auto){++count;});
    return count;
})();

}

///Class which implements NAMED_ENUM macro
template<typename EnumType, auto string_fn>
class EnumToStringLookupTable: public StaticLookupTable<EnumType, std::string_view, _named_enum_details::enumCountItems<EnumType, string_fn> > {

public:
    using Super = StaticLookupTable<EnumType, std::string_view, _named_enum_details::enumCountItems<EnumType, string_fn> >;
    using EnumUnderlyingType = typename Super::EnumUnderlyingType;


    static constexpr int string_area_size = ([]{
        int count = 0;
        std::string_view text = string_fn();
        _named_enum_details::enumSyntaxParser<std::underlying_type_t<EnumType> >(text.begin(), text.end(),[&](auto a, auto){ count += a.size()+1;});
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
        _named_enum_details::enumSyntaxParser<std::underlying_type_t<EnumType> >(text.begin(), text.end(),[&](std::string_view text, EnumUnderlyingType idx ){
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

/**
 * The macro declares enum Typename with values specified as other arguments, separated by comma. The
 * declaration mimics standard enum declaration. It is also possible to change assignemnt
 * values to each enum as long as it is single decimal, octal or hexadecimal constanct
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
 * Along with the declaration of the enum type, the NamedEnum_Typename is also declared, which
 * extends NamedEnum class. It automatically registers string value for each enum value. To
 * use this class, you need to create an instance. It is recommended to declare the instance
 * as constexpr, because the compiler can generate lookup table during compilation
 */
#define NAMED_ENUM(Typename, ...) enum class Typename { __VA_ARGS__}; \
using NamedEnum_##Typename =  ::coroserver::EnumToStringLookupTable<Typename, []{return #__VA_ARGS__;}>;


}


#endif /* SRC_COROSERVER_STATIC_LOOKUP_H_ */
