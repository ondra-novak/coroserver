#pragma once
#include <algorithm>
#include <iterator>
#include <concepts>


///Implements a statically allocated bidirectional search table between values of different types
/**
 * @tparam KeyType Type of the key. It is required that the type is an integral type or values of this type can be compared and sorted.
 * @tparam ValueType Type of the value. It is recommended that this type defines a comparison operator, ideally making values sortable.
 * @tparam Count Number of elements in the table. This value needs to be known in advance. For greater convenience, the function makeStaticLookupTable() can be used, which allows the class to be constructed while the number of elements is automatically calculated.
 *
 * @code
 * constexpr auto intToStringLookup = makeStaticLookupTable<int, std::string_view>({
 *    {1, "one"},
 *    {2, "two"},
 *    {3, "three"}
 * });
 * @endcode
 *
 * If you need to declare the instance using the extern declaration, you cannot use makeStaticLookupTable.
 * Instead, you need to manually count the items.
 *
 * @code
 * extern StaticLookupTable<int, std::string_view, 3> intToStringLookup;
 * @endcode
 *
*/
template<std::totally_ordered KeyType, typename ValueType, int Count>
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
        KeyType key;
        ///value type - value
        ValueType value;
    };

    ///contains underlying enum type, for non-enum type, contains KeyType
    using EnumUnderlyingType = typename std::conditional_t<std::is_enum_v<KeyType>,std::underlying_type<std::decay_t<KeyType> >, std::decay<KeyType> >::type;
    ///Contains true, if ValueType can be ordered. For large set of items it is better to support ordering, otherwise fullrow scan is used.
    constexpr static bool is_ordered =  std::totally_ordered<ValueType>;
    ///Contains true, if ValueType can be compared
    constexpr static bool is_equal_comparable=  std::equality_comparable<ValueType>;
    ///contains true, if the KeyType can be sequence, so it need to be integral type or enum
    constexpr static bool can_be_sequence =std::is_integral_v<KeyType> || std::is_enum_v<KeyType>;

    constexpr static int log2(int x) {
        int c = 0;
        while (x) {++c; x>>=1;}
        return c;
    }

    ///Calculates count of search cycles for lower_bound search
    constexpr static int search_cycles = log2(Count);

    ///Declaration of storage
    /**
     * The main reason for using a union is to avoid requiring
     * the declaration of a default constructor. A union allows
     * for initialization at a later point. It works similarly
     * to std::optional, but without the need to keep track
     * of whether the value is initialized, as this information
     * can be inferred from the execution of further code.
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
    constexpr StaticLookupTable(const Item (&items)[Count]) {
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
            std::construct_at(&_items[i].x, Item(items[order[i]]));
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
    static constexpr KeyType defaultEnum = {};

    /// Get value registered for given enum value
    /**
     * @param evalue enum value
     * @param defval default value in case when enum is not registered
     * @return found value or default value
     *
     * @note if the underlying value of each registered enum is sequence of numbers 1,2,3,4,5,6, the lookup
     * has O(1) complexity, otherwise it has O(log n) complexity
    */
    constexpr const ValueType &get(const KeyType &evalue, const ValueType &defval = defaultValue) const {
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
    constexpr const KeyType &get(const ValueType &v, const KeyType &defval = defaultEnum) const {
        auto iter = find(v);
        if (iter == end()) return defval;
        else return iter->key;
    }

    ///@see get();
    constexpr const ValueType &operator[](const KeyType &evalue) const {return get(evalue);}
    ///@see get();
    constexpr const KeyType &operator[](const ValueType &v) const {return get(v);}


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
    constexpr Iterator find(const KeyType &evalue) const {
        if constexpr(can_be_sequence) {
            if (_sequence) {
                if (evalue >= _items[0]->key && evalue <= _items[Count-1]->key) {
                    int offset = static_cast<int>(static_cast<EnumUnderlyingType>(evalue) - static_cast<EnumUnderlyingType>(_items[0]->key));
                    return Iterator(_items+offset);
                }
                return end();
            }
        }
        return Iterator(_items+lower_bound([&](int idx) { return _items[idx]->key; }, evalue));
    }

    ///finds record for given  value
    /**
     * @param evalue v to find
     * @return returns iterator or end() if not found
    */
    constexpr Iterator find(const ValueType &v) const {
        if constexpr(is_ordered) {
            int pos = lower_bound([&](int idx) { return _items[_valueIndex.pos[idx]]->value; }, v);
            if (pos == Count) return Iterator(_items+Count);
            else return Iterator(_items+_valueIndex.pos[pos]);
        } else if constexpr(is_equal_comparable) {
            int it = 0;
            while (it < Count && _items[it]->value != v) ++it;
            return Iterator(_items+it);
        } else
            return end();
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
            std::sort(std::begin(_valueIndex.pos), std::end(_valueIndex.pos), [&](int a, int b) {
                return _items[a]->value < _items[b]->value;
            });
        }
    }
    ///
    constexpr bool is_sequence() const {
        if constexpr(can_be_sequence) {
            KeyType itr = _items[0]->key;
            for (int i = 1; i < Count; i++) {
                if constexpr(std::is_enum_v<KeyType>) {
                    itr = static_cast<KeyType>(static_cast<EnumUnderlyingType>(itr)+1);
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
        //branchless lower_bound
        int first = 0;
        int c = Count;
        for (int i = 0; i < search_cycles; i++) { //unrolled out by compiler
            auto step = c / 2;
            auto it = first + step;
            int cmp_res = ((field(it) < value)?-1:0);          // 0xFFFFFFFF when true
            first += (it - first) & cmp_res;             //cmp_res?it:first
            c = step + ((c - 2*step) & cmp_res); //cmp_res?c-step:step
        }
        first+=c;
        int r1 = (first >= Count);                       // first>=Count?1:0
        int r2 = field(first-r1) != value;               // !found?1:0
        return Count - ((Count - first) & ((r1|r2)-1));  // (r1 || r2)?Count:first
    }

};

///Create StaticLookupTableInstance instance
/**
 * @tparam KeyType type of enum value
 * @tparam ValueType type of associated value
 * @param x array of items key-value pairs - {enum, value}
*/
template<typename KeyType, typename ValueType, int N>
inline constexpr auto makeStaticLookupTable(const typename StaticLookupTable<KeyType, ValueType, N>::Item (&x)[N]) {
    return StaticLookupTable<KeyType,ValueType, N>(x);
}




