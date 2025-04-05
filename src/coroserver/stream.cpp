#include "stream.hpp"

#include "context.hpp"

namespace coroserver {


Context Stream::get_context() const {
    return _ptr->get_context();
}
}
