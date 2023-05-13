include_directories(AFTER ${CMAKE_CURRENT_LIST_DIR}/src)
add_subdirectory (${CMAKE_CURRENT_LIST_DIR}/src/coroserver EXCLUDE_FROM_ALL)
add_subdirectory (${CMAKE_CURRENT_LIST_DIR}/version EXCLUDE_FROM_ALL)
