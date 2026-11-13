cmake_minimum_required(VERSION 3.22.1)
project(turok2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL2 desde el submódulo (fijado a release-2.32.10). Binary dir explícito: está fuera de src/.
# Opciones SDL_SHARED/SDL_STATIC/SDL_TEST se pasan desde android/app/build.gradle.kts.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/SDL2 ${CMAKE_BINARY_DIR}/SDL2)

# Módulos independientes de plataforma (también se compilan en el host para tests)
add_subdirectory(vfs)
add_subdirectory(loc)
add_subdirectory(render)
add_subdirectory(audio)
add_subdirectory(ui)

# Debe ser SHARED: SDLActivity carga libturok2.so y busca SDL_main en ella
add_library(turok2 SHARED
    main.cpp
    title_screen.cpp
    vfs/vfs_sdl.cpp
    render/gl/gl_renderer.cpp
    audio/audio_sdl.cpp
    input/input_sdl.cpp)
# src/ como raíz de includes de la app (log.hpp, "audio/...", "ui/...")
target_include_directories(turok2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Flags estrictos solo para nuestro código (no se imponen a SDL ni a miniz)
turok2_strict(turok2)

target_link_libraries(turok2 PRIVATE
    turok2_vfs turok2_loc turok2_render_core turok2_audio_core turok2_ui_core
    SDL2::SDL2 GLESv3 log)
