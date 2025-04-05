#pragma once
#include "context.hpp"
#include "stream.hpp"


namespace coroserver {


///connect to host and port
/**
 * @param ctx context
 * @param host host
 * @param def_port default port (port can be specified in host after :)
 * @return connected stream
 *
 * @note connection is performed at background. The first co_await on read on write
 * check whether connection has been established, so any potential error is
 * thrown there
 */
Stream connect(Context ctx, std::string host, std::string def_port);
///Connect stdin and stdout
/**
 * Allows to connect with master process through stdin and stdout
 * @param ctx context
 * @return connected stream
 *
 * @note The stream receives full exclusive access on stdin and stdout. Standard
 * handles are closed
 */
Stream connect_stdinout(Context ctx);



}
