#pragma once
#include <memory>


template<typename T, std::size_t res, std::invocable<T *> Fn, typename ... Args>
auto alloc_on_stack_fixed(Fn &&callback, std::size_t sz, Args && ... args) noexcept {
    char buffer[res*sizeof(T)];
    T *uninit = reinterpret_cast<T *>(buffer);
    for (std::size_t i = 0; i < sz; ++i) std::construct_at(uninit+i, args...);
    auto deleter = [sz](T *ptr){
        for (std::size_t i = 0; i < sz; ++i) {
            std::destroy_at(ptr+i);
        }
    };
    std::unique_ptr<T, decltype(deleter)> ptr(uninit,deleter);
    return std::forward<Fn>(callback)(ptr.get());
}

template<typename T, std::invocable<T *> Fn, typename ... Args>
auto alloc_on_stack_dyn(Fn &&callback, std::size_t sz,  Args && ... args) noexcept {
    T *uninit = static_cast<T *>(::operator new(sz*sizeof(T)));
    for (std::size_t i = 0; i < sz; ++i) std::construct_at(uninit+i, args...);
    auto deleter = [sz](T *ptr){
        for (std::size_t i = 0; i < sz; ++i) {
            std::destroy_at(ptr+i);
        }
        ::operator delete(ptr);
    };
    std::unique_ptr<T, decltype(deleter)> ptr(uninit,deleter);
    return std::forward<Fn>(callback)(ptr.get());
}


///Alloc elements on stack and call a function
/**
 *
 * @tparam T type of element
 * @param sz count of element
 * @param fn functon called with a pointer to a first element
 * @param arguments for constructor
 * @return return value of the function
 */
template<typename T, std::invocable<T *> Fn, typename ... Args>
auto alloc_on_stack(std::size_t sz, Fn &&fn, Args && ... args) noexcept  {
    if (sz <= 3)
        return alloc_on_stack_fixed<T,3>(std::forward<Fn>(fn), sz, std::forward<Args>(args)...);
    if (sz <= 5)
        return alloc_on_stack_fixed<T,5>(std::forward<Fn>(fn), sz, std::forward<Args>(args)...);
    if (sz <= 8)
        return alloc_on_stack_fixed<T,8>(std::forward<Fn>(fn), sz, std::forward<Args>(args)...);
    if (sz <= 13)
        return alloc_on_stack_fixed<T,13>(std::forward<Fn>(fn), sz, std::forward<Args>(args)...);
    if (sz <= 21)
        return alloc_on_stack_fixed<T,21>(std::forward<Fn>(fn), sz, std::forward<Args>(args)...);
    if (sz <= 34)
        return alloc_on_stack_fixed<T,34>(std::forward<Fn>(fn), sz, std::forward<Args>(args)...);
    if (sz <= 55)
        return alloc_on_stack_fixed<T,55>(std::forward<Fn>(fn), sz, std::forward<Args>(args)...);
    if (sz <= 89)
        return alloc_on_stack_fixed<T,89>(std::forward<Fn>(fn), sz, std::forward<Args>(args)...);
    if (sz <= 144)
        return alloc_on_stack_fixed<T,144>(std::forward<Fn>(fn), sz, std::forward<Args>(args)...);
    if (sz <= 233)
        return alloc_on_stack_fixed<T,233>(std::forward<Fn>(fn), sz, std::forward<Args>(args)...);
    if (sz <= 377)
        return alloc_on_stack_fixed<T,377>(std::forward<Fn>(fn), sz, std::forward<Args>(args)...);

    return alloc_on_stack_dyn<T, Fn>(std::forward<Fn>(fn), sz, std::forward<Args>(args)...);
}
