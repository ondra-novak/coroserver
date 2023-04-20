/*
 * coro_alloc.h
 *
 *  Created on: 19. 4. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_CORO_ALLOC_H_
#define SRC_COROSERVER_CORO_ALLOC_H_
#include <atomic>
#include <cassert>
#include <cstddef>

namespace coroserver {


class stackful_storage {
public:
    stackful_storage(std::atomic<std::size_t>  &max_alloc):_max_alloc(max_alloc) {}

    void *alloc(std::size_t sz) {
        void *ptr = alloc_block(sz+sizeof(stackful_storage *));
        stackful_storage **x = reinterpret_cast<stackful_storage **>(reinterpret_cast<char *>(ptr)+sz);
        *x = this;
        return ptr;
    }

    static void dealloc(void *ptr, std::size_t sz) {
        stackful_storage **x = reinterpret_cast<stackful_storage **>(reinterpret_cast<char *>(ptr)+sz);
        auto inst = *x;
        inst->dealloc_block(ptr, sz+sizeof(stackful_storage *));
    }

    stackful_storage(const stackful_storage &) = delete;
    stackful_storage &operator=(const stackful_storage &) = delete;
    stackful_storage(stackful_storage &o)
            :buff(o.buff)
            ,alloc_size(o.alloc_size)
            ,capacity(o.capacity)
            ,over_alloc(o.over_alloc)
            ,_max_alloc(o._max_alloc) {
        o.buff = nullptr;
        o.alloc_size = 0;
        o.capacity = 0;
        o.over_alloc = 0;
    }

    ~stackful_storage() {
        ::operator delete(buff);
    }


protected:
    char *buff = 0;
    std::size_t alloc_size = 0;
    std::size_t capacity = 0;
    std::size_t over_alloc = 0;
    std::atomic<std::size_t> &_max_alloc;

    void *alloc_block(std::size_t sz) {
        if (alloc_size == 0) {
            std::size_t cap = std::max<std::size_t>(sz,_max_alloc.load(std::memory_order_relaxed));
            if (cap > capacity) {
                ::operator delete(buff);
                capacity = cap;
                buff = reinterpret_cast<char *>(::operator new(capacity));
            }
            alloc_size+=sz;
            return buff;
        } else if (alloc_size+sz < capacity) {
            char *p = buff+alloc_size;
            alloc_size += sz;
            return p;
        } else {
            over_alloc += sz;
            auto total = over_alloc + over_alloc;
            std::size_t c = _max_alloc.load(std::memory_order_relaxed);
            while (c < total && !_max_alloc.compare_exchange_weak(c, total, std::memory_order_relaxed));
            return ::operator new(sz);
        }
    }

    void dealloc_block(void *ptr, std::size_t sz) {
        if (over_alloc) {
            assert(over_alloc >= sz);
            over_alloc-=sz;
            ::operator delete(ptr);
        } else {
            assert(alloc_size >= sz);
            assert(reinterpret_cast<char *>(ptr) >= buff && reinterpret_cast<char *>(ptr) <= buff+alloc_size-sz);
            alloc_size-=sz;
        }
    }

};

}



#endif /* SRC_COROSERVER_CORO_ALLOC_H_ */
