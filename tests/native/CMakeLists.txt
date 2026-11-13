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
add_subdirectory(${SRC_DIR}/audio ${CMAKE_BINARY_DIR}/audio)
add_subdirectory(${SRC_DIR}/ui ${CMAKE_BINARY_DIR}/ui)
add_subdirectory(${SRC_DIR}/render3d ${CMAKE_BINARY_DIR}/render3d)
add_subdirectory(${SRC_DIR}/physics ${CMAKE_BINARY_DIR}/physics)
add_subdirectory(${SRC_DIR}/game ${CMAKE_BINARY_DIR}/game)
include(${SRC_DIR}/cmake/Strict.cmake)

add_executable(vfs_tests test_main.cpp test_vfs.cpp)
target_link_libraries(vfs_tests PRIVATE turok2_vfs miniz)
turok2_strict(vfs_tests)

add_executable(loc_tests test_main.cpp test_loc.cpp)
target_link_libraries(loc_tests PRIVATE turok2_loc)
turok2_strict(loc_tests)

add_executable(render_tests test_main.cpp test_png.cpp test_kfont.cpp test_text_layout.cpp)
target_link_libraries(render_tests PRIVATE turok2_render_core turok2_loc miniz)
turok2_strict(render_tests)

add_executable(audio_tests test_main.cpp test_wav.cpp test_ogg.cpp test_mixer.cpp)
target_link_libraries(audio_tests PRIVATE turok2_audio_core)
turok2_strict(audio_tests)

add_executable(ui_tests test_main.cpp test_menu_nav.cpp test_settings.cpp test_menu_controller.cpp
    test_touch_sticks.cpp)
target_link_libraries(ui_tests PRIVATE turok2_ui_core)
turok2_strict(ui_tests)

add_executable(render3d_tests test_main.cpp test_kexbin.cpp test_map.cpp test_static_mesh.cpp test_kmat.cpp
    test_level_builder.cpp test_camera.cpp test_atmosphere.cpp test_skinned_mesh.cpp test_anim.cpp
    test_skeleton.cpp test_actors.cpp test_real_kpf.cpp)
target_link_libraries(render3d_tests PRIVATE turok2_render3d_core turok2_vfs miniz)
turok2_strict(render3d_tests)

add_executable(physics_tests test_main.cpp test_physics_geometry.cpp test_sector_world.cpp
    test_player_controller.cpp test_real_physics.cpp)
target_link_libraries(physics_tests PRIVATE turok2_physics_core turok2_vfs miniz)
turok2_strict(physics_tests)

add_executable(game_tests test_main.cpp test_weapon_view.cpp)
target_link_libraries(game_tests PRIVATE turok2_game_core)
turok2_strict(game_tests)

enable_testing()
add_test(NAME vfs COMMAND vfs_tests)
add_test(NAME loc COMMAND loc_tests)
add_test(NAME render COMMAND render_tests)
add_test(NAME audio COMMAND audio_tests)
add_test(NAME ui COMMAND ui_tests)
add_test(NAME render3d COMMAND render3d_tests)
add_test(NAME physics COMMAND physics_tests)
add_test(NAME game COMMAND game_tests)
