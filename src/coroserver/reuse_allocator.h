#pragma once
#ifndef SRC_COROSERVER_REUSE_ALLOCATOR_H_
#define SRC_COROSERVER_REUSE_ALLOCATOR_H_

namespace coroserver {

template<typename T>
struct ReuseAllocator {

    using value_type = T;

    union CacheItem {
        char _data[sizeof(T)];
        CacheItem *_next;
    };

    CacheItem *_cache = nullptr;

    ReuseAllocator() = default;
    ReuseAllocator(const ReuseAllocator &) {} //default
    template<typename U>
    ReuseAllocator(const ReuseAllocator<U> &) {} //default
    ~ReuseAllocator() {
        while (_cache) {
            auto p = _cache;
            _cache = _cache->_next;
            delete p;
        }
    }

    T *allocate(int n) {
       if (n != 1) throw std::bad_alloc();
       CacheItem *p;
       if (_cache) {
           p = _cache;
           _cache = _cache->_next;

       } else {
           p = new CacheItem;
       }
       return reinterpret_cast<T *>(p);
    }

    void deallocate(T *ptr, int) {
        CacheItem *itm = reinterpret_cast<CacheItem *>(ptr);
        itm->_next = _cache;
        _cache = itm;
    }
};

template<typename T>
struct CacheFriendlyAllocator {

    using value_type = T;



    union Item {
        T _payload;
        Item *_next_free;

        Item() {};
        ~Item() {};
    };

    union CacheItem {
        char _data[sizeof(T)];
        CacheItem *_next;
    };


    std::deque<Item> _items = {};
    Item *_first_free = nullptr;


    CacheItem *_cache = nullptr;

    CacheFriendlyAllocator() = default;
    CacheFriendlyAllocator(const CacheFriendlyAllocator &) {} //default
    template<typename U>
    CacheFriendlyAllocator(const CacheFriendlyAllocator<U> &) {} //default

    T *allocate(int n) {
       if (n != 1) {
           return reinterpret_cast<T *>(::operator new(sizeof(T)*n));
       }
       if (_first_free) {
           Item *x = _first_free;
           _first_free = x->_next_free;
           return &x->_payload;
       } else {
           _items.emplace_back();
           Item &x = _items.back();
           return &x._payload;
       }
    }

    void deallocate(T *ptr, int n) {
        if (n != 1) {
            ::operator delete(ptr);
            return;
        }
        Item *x = reinterpret_cast<Item *>(ptr);
        x->_next_free = _first_free;
        _first_free = x;
    }
};


}




#endif /* SRC_COROSERVER_REUSE_ALLOCATOR_H_ */
