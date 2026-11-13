# Núcleo del VFS: independiente de SDL/Android para poder probarlo en el host
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/Strict.cmake)

if(NOT TARGET miniz)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../third_party/miniz ${CMAKE_BINARY_DIR}/miniz)
endif()

add_library(turok2_vfs STATIC vfs.cpp vfs_zip.cpp vfs_dir.cpp)
target_include_directories(turok2_vfs PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(turok2_vfs PRIVATE miniz)
set_target_properties(turok2_vfs PROPERTIES POSITION_INDEPENDENT_CODE ON)
turok2_strict(turok2_vfs)
