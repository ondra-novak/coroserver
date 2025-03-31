cmake_minimum_required(VERSION 3.5)


set(COMMON_FILES
    network.cpp
    ssl_stream.cpp    
    context_linux.cpp
    basic_stream.cpp
    server.cpp
    client.cpp
)

if(MSVC)
    set(PLATFORM_SPECIFIC_FILES network_windows.cpp)
else()
    set(PLATFORM_SPECIFIC_FILES network_linux.cpp)
endif()

add_library(coroserver ${COMMON_FILES} ${PLATFORM_SPECIFIC_FILES})



