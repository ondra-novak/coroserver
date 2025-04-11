#include "stream.hpp"

#include "context.hpp"

namespace coroserver {


Context Stream::get_context() const {
    return _ptr->get_context();
}
Context StreamProxy::get_context() const
{
   return _s.get_context();
}
}
