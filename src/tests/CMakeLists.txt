cmake_minimum_required(VERSION 3.1)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/)
#file(GLOB testFiles "*.cpp")
set(testFiles
    compile_test.cpp
    peername.cpp
    server_and_connect.cpp
    read_until.cpp
    chartest.cpp
    json.cpp
    tcp_test.cpp
    chunked_stream.cpp
    limited_stream.cpp
    http_server.cpp
    prefixmap.cpp
    named_enum.cpp
    query.cpp
    http_client.cpp
    mt_stream.cpp
    shared_lockable_ptr.cpp
)

link_libraries(
    coroserver
    ${STANDARD_LIBRARIES}
)

foreach (testFile ${testFiles})
    string(REGEX MATCH "([^\/]+$)" filename ${testFile})
    string(REGEX MATCH "[^.]*" executable_name tests_${filename})
    add_executable(${executable_name} ${testFile})
    target_link_libraries(${executable_name} ${STANDARD_LIBRARIES} )
    add_test(NAME "tests/${filename}" COMMAND ${executable_name})
endforeach ()
