cmake_minimum_required(VERSION 3.5)


set(COMMON_FILES
    network.cpp
    socket_stream.cpp
    ssl_stream.cpp
)

if(MSVC)
    set(PLATFORM_SPECIFIC_FILES network_windows.cpp)
else()
    set(PLATFORM_SPECIFIC_FILES network_linux.cpp)
endif()

add_library(coroserver ${COMMON_FILES} ${PLATFORM_SPECIFIC_FILES})



