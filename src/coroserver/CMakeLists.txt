cmake_minimum_required(VERSION 3.1)

add_library(coroserver
	peername.cpp
	stream.cpp
	socket_stream.cpp
	io_context.cpp
	poller_epoll.cpp
	memstream.cpp
	chunked_stream.cpp
	limited_stream.cpp
	json/json.cpp
	http_common.cpp
	http_server_request.cpp
	http_server.cpp
	http_static_page.cpp
	websocket.cpp
	http_ws_server.cpp

)
add_dependencies(coroserver coroserver_version)

