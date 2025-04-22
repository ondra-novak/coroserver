cmake_minimum_required(VERSION 3.5)


set(COMMON_FILES
    stream.cpp
    ssl_stream.cpp        
    basic_stream.cpp
    chunked_stream.cpp
    server.cpp
    client.cpp
    process.cpp
    stream.cpp
    http_common.cpp
    http_server_request.cpp
    http_client_request.cpp
    ws_defs.cpp
    websocket_stream.cpp
    websocket_server.cpp
)

if(MSVC)
    set(PLATFORM_SPECIFIC_FILES context_windows.cpp)
else()
    set(PLATFORM_SPECIFIC_FILES context_linux.cpp)
endif()

add_library(coroserver ${COMMON_FILES} ${PLATFORM_SPECIFIC_FILES})



