cmake_minimum_required(VERSION 3.5)


set(COMMON_FILES
    network.cpp
    ssl_stream.cpp        
    basic_stream.cpp
    chunked_stream.cpp
    server.cpp
    client.cpp
)

if(MSVC)
    set(PLATFORM_SPECIFIC_FILES context_windows.cpp)
else()
    set(PLATFORM_SPECIFIC_FILES context_linux.cpp)
endif()

add_library(coroserver ${COMMON_FILES} ${PLATFORM_SPECIFIC_FILES})



