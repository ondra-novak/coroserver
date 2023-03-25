cmake_minimum_required(VERSION 3.1)

add_library(coroserver
	peername.cpp
)
add_dependencies(coroserver coroserver_version)

