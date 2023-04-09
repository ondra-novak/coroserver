cmake_minimum_required(VERSION 3.1)

add_library(coroserver
	peername.cpp
	stream.cpp
	socket_stream.cpp
	io_context.cpp
	poller_epoll.cpp
	memstream.cpp
	json/json.cpp
)
add_dependencies(coroserver coroserver_version)

