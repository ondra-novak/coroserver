cmake_minimum_required(VERSION 3.1)

add_library(coroserver
	coroserver.cpp
)
add_dependencies(coroserver coroserver_version)

