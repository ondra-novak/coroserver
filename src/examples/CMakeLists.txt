cmake_minimum_required(VERSION 3.1)


link_libraries(
    coroserver
    ${STANDARD_LIBRARIES}
)

add_executable(simple_web_server simple_web_server.cpp)
add_executable(simple_web_server2 simple_web_server2.cpp)
