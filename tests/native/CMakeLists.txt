# Tests nativos en el host (sin SDL ni Android). Uso:
#   cmake -S tests/native -B build-host && cmake --build build-host && ctest --test-dir build-host --output-on-failure
cmake_minimum_required(VERSION 3.22)
project(turok2_host_tests C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

option(TURK2_SANITIZE "Compilar con ASan + UBSan" ON)
if(TURK2_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
add_subdirectory(${SRC_DIR}/vfs ${CMAKE_BINARY_DIR}/vfs)
add_subdirectory(${SRC_DIR}/loc ${CMAKE_BINARY_DIR}/loc)
add_subdirectory(${SRC_DIR}/render ${CMAKE_BINARY_DIR}/render)
include(${SRC_DIR}/cmake/Strict.cmake)

add_executable(vfs_tests test_main.cpp test_vfs.cpp)
target_link_libraries(vfs_tests PRIVATE turok2_vfs miniz)
turok2_strict(vfs_tests)

add_executable(loc_tests test_main.cpp test_loc.cpp)
target_link_libraries(loc_tests PRIVATE turok2_loc)
turok2_strict(loc_tests)

add_executable(render_tests test_main.cpp test_png.cpp)
target_link_libraries(render_tests PRIVATE turok2_render_core miniz)
turok2_strict(render_tests)

enable_testing()
add_test(NAME vfs COMMAND vfs_tests)
add_test(NAME loc COMMAND loc_tests)
add_test(NAME render COMMAND render_tests)
