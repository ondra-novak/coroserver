#include <memory>

template<typename T>
class cache_ptr : public std::unique_ptr<T> {
public:
    using std::unique_ptr<T>::unique_ptr;
    cache_ptr(const )
}