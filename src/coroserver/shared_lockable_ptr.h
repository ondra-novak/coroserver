#pragma once
#ifndef SRC_COROSERVER_SHARED_LOCKABLE_PTR_H_
#define SRC_COROSERVER_SHARED_LOCKABLE_PTR_H_
#include <cassert>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <functional>

namespace coroserver {


template<typename T>
class weak_lockable_ptr;

///Shared pointer with internally implemented lock with shared and exclusive locking semantics
/**
 * Works similar as std::shared_ptr<T>, however you cannot dereference the object directly. You
 * need to be locked or locked for share to retrieve the actual pointer. You still retrieve
 * an unique_ptr, which holds the lock until the pointer is released
 *
 * @tparam T type of object shared with lock
 */
template<typename T>
class shared_lockable_ptr {
public:

    ///Internal context
    /** The Context is placed inside of shared_ptr's control block. It uses
     * Deleter structure where additional data are stored (this means, that
     * standard Deleter cannot be used
     */
    class Context {
    public:
        Context() = default;
        Context(const Context &other)
            :_custom_deleter(other._custom_deleter)
            ,_inplace_ptr(other._inplace_ptr) {
            //mutex should be inactive
            assert(!_mx.has_value());
        }
        ///contains mutex - must be activated later
        std::optional<std::shared_mutex> _mx;
        ///custom deleter, for technical reason, function is only option
        std::function<void(T *)> _custom_deleter;
        ///contains original pointer when inplace allocation is used
        T *_inplace_ptr = nullptr;

        ///Deleter's mandatory function
        /**
         * @param ptr pointer to object. however the pointer is nullptr, when inplace
         * construction is used. In this case, _inplace_ptr is used to destruct the object
         * stored in control structure's memory
         */
        void operator()(T *ptr) {
            if (_inplace_ptr) _inplace_ptr->~T();
            if (ptr) {
                if (_custom_deleter) _custom_deleter(ptr);
                else delete ptr;
            }
        }
        ///activate lock (constructs the lock)
        /** called when shared_ptr is ready to be returned */
        void activate_lock() {
            _mx.emplace();
        }
        ///registers instance allocated during in-place allocation
        /**
         * @param ptr pointer to object allocated in-place
         */
        void register_inplace_instance(T *ptr) {
            _inplace_ptr = ptr;
        }
        ///registers custom deleter
        void activate_custom_deleter(std::function<void(T *)> &&fn) {
            _custom_deleter = std::move(fn);
        }
    };

    ///This allocator is used to allocate the control structure, however it also allocates extra space for the object itself
    template<typename X>
    class InPlaceAlocator {
    public:
        using value_type = X;
        ///Initialize the allocator
        /**
         * Because it is impossible access the allocator later and also it is
         * impossible to retrieve memory location of memory space for the object, this pointer
         * must point to a variable, where the location will be stored. This pointer is used
         * only during allocation

         * @param target_ptr pointer to variable, where the pointer will be stored
         */
        InPlaceAlocator(T **target_ptr):_target_ptr(target_ptr) {}
        InPlaceAlocator(const InPlaceAlocator &) = default;
        template<typename Y>
        InPlaceAlocator(const InPlaceAlocator<Y> &y):_target_ptr(y._target_ptr) {}

        ///allocate memory for the control structure
        X *allocate(int n) {
            assert(n == 1);
            int need_sz = sizeof(X) + sizeof(T);
            void *ptr = ::operator new(need_sz);
            *_target_ptr = reinterpret_cast<T *>(reinterpret_cast<char *>(ptr)+sizeof(X));
            return reinterpret_cast<X *>(ptr);
        }
        ///deallocate memory for the control structure
        void deallocate(void *ptr, int n) {
            assert(n == 1);
            ::operator delete(ptr);
        }

        T **_target_ptr;
    };


    ///Deleter used in unique_ptr returned by lock() or lock_shared()
    /**
     * @tparam shared set true for shared lock, false for exclusive lock
     */
    template<bool shared>
    class UnlockDeleter {
    public:
        UnlockDeleter(std::shared_ptr<T> ptr):_ptr(std::move(ptr)) {}
        UnlockDeleter(UnlockDeleter &&) = default;
        UnlockDeleter(const UnlockDeleter &) = delete;
        ~UnlockDeleter() {
            if (_ptr) {
                auto &lk = get_lock(_ptr);
                if constexpr(shared) {
                    lk.unlock_shared();
                } else {
                    lk.unlock();
                }
            }
        }
        void operator()(T *) {/* empty */}
        void operator()(const T *) {/* empty */}
    protected:
        std::shared_ptr<T> _ptr;

    };

    using exclusive_ptr = std::unique_ptr<T, UnlockDeleter<false> >;
    using shared_ptr = std::unique_ptr<const T, UnlockDeleter<true> >;

    ///lock exclusive
    /**
     * @return unique_ptr with actual pointer. Releasing the pointer causes release of the lock
     */
    exclusive_ptr lock() {
        if (_ptr) {
            get_lock(_ptr).lock();
            return exclusive_ptr(_ptr.get(), _ptr);
        } else {
            return exclusive_ptr(nullptr, _ptr);
        }
    }

    ///lock shared
    /**
     * @return unique_ptr with actual pointer (const). Releasing the pointer causes release of the lock
     */
    shared_ptr lock_shared() const {
        if (_ptr) {
            get_lock(_ptr).lock_shared();
            return shared_ptr(_ptr.get(), _ptr);
        } else {
            return shared_ptr(nullptr, _ptr);
        }
    }

    ///Construct empty
    shared_lockable_ptr() = default;


    ///Construct for existing object
    shared_lockable_ptr(T *ptr):_ptr(ptr, Context{}) {
        Context *ctx = std::get_deleter<Context>(_ptr);
        ctx->activate_lock();
    }

    ///Construct for existing object with custom deleter
    shared_lockable_ptr(T *ptr, std::function<void(T *)>  deleter)
        :_ptr(ptr, Context{}) {
        Context *ctx = std::get_deleter<Context>(_ptr);
        ctx->activate_custom_deleter(std::move(deleter));
        ctx->activate_lock();
    }


    ///Allocate new object
    /**
     * @note for convince, use make_shared_lockable<Type>()
     */
    enum TagCreateInstance {_tagCreateInstance};///< _tagCreateInstance
    template<typename ... Args>
    shared_lockable_ptr(TagCreateInstance, Args && ... args)
    {
        T *place = nullptr;
        _ptr = std::shared_ptr<T>(nullptr, Context{}, InPlaceAlocator<T>(&place));
        std::construct_at(place, std::forward<Args>(args)...);
        _ptr = std::shared_ptr<T>(std::move(_ptr), place);
        Context *ctx = std::get_deleter<Context>(_ptr);
        ctx->register_inplace_instance(place);
        ctx->activate_lock();
    }


    explicit operator bool() const {return static_cast<bool>(_ptr);}

    auto use_count() const {return _ptr.use_count();}

    auto unique() const {return _ptr.unique();}

    void reset() {_ptr.reset();}


protected:
    mutable std::shared_ptr<T> _ptr;

    shared_lockable_ptr(std::shared_ptr<T> ptr):_ptr(std::move(ptr)) {}


    static std::shared_mutex &get_lock(std::shared_ptr<T> &ptr) {
        Context *d = std::get_deleter<Context>(ptr);
        return *d->_mx;
    }

    friend class weak_lockable_ptr<T>;
};



template<typename T>
class weak_lockable_ptr {
public:
    weak_lockable_ptr() = default;
    weak_lockable_ptr(shared_lockable_ptr<T> p):_ptr(std::move(p._ptr)) {}

    shared_lockable_ptr<T> lock() const {
        return shared_lockable_ptr<T>(_ptr.lock());
    }
protected:
    std::weak_ptr<T> _ptr;
};

template<typename T, typename ... Args>
shared_lockable_ptr<T> make_shared_lockable(Args && ... args) {
    using SP = shared_lockable_ptr<T>;
    return  SP(SP::_tagCreateInstance, std::forward<Args>(args)...);
}

}



#endif /* SRC_COROSERVER_SHARED_LOCKABLE_PTR_H_ */
