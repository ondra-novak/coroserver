cmake_minimum_required(VERSION 3.1)


link_libraries(
    coroserver
    ssl
    crypto    
    ${STANDARD_LIBRARIES}
)

add_executable(simple_web_server simple_web_server.cpp)
add_executable(simple_web_server2 simple_web_server2.cpp)
add_executable(simple_secure_web_server simple_secure_web_server.cpp)
#add_executable(websocket_server websocket_server.cpp)
#add_executable(websocket_client websocket_client.cpp)
add_executable(https_client https_client.cpp)
