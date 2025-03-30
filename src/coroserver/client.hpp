#pragma once
#include "context.hpp"
#include "stream.hpp"


namespace coroserver {


Stream connect(Context ctx, std::string host, std::string def_port);

Stream connect(Context ctx, SpecialDevice specdev);


}
