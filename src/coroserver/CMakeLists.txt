cmake_minimum_required(VERSION 3.5)

add_library(coroserver
	peername.cpp
	async_engine.cpp
	async_engine_epoll.cpp
 #	epoll.cpp
 #  local_stream.cpp
	stream.cpp
	socket_stream.cpp
	context.cpp
    event_fd.cpp
    ssl_common.cpp
    ssl_stream.cpp
#	poller_epoll.cpp
    memstream.cpp
    chunked_stream.cpp
    limited_stream.cpp
#   json/json.cpp
    http_common.cpp
    http_server_request.cpp
    http_server.cpp
    http_static_page.cpp
    websocket.cpp
    websocket_stream.cpp
    http_ws_server.cpp
    http_ws_client.cpp
    ssl_http_support.cpp
    http_client_request.cpp
    http_client.cpp
#   umq_stream_connection.cpp
#   message_stream.cpp
#   umq_peer.cpp
    
)
add_dependencies(coroserver coroserver_version libcoro_single_header)


