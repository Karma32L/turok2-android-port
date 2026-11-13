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

# Debe ser SHARED: SDLActivity carga libturok2.so y busca SDL_main en ella
add_library(turok2 SHARED main.cpp vfs/vfs_sdl.cpp)

# Flags estrictos solo para nuestro código (no se imponen a SDL ni a miniz)
turok2_strict(turok2)

target_link_libraries(turok2 PRIVATE turok2_vfs turok2_loc SDL2::SDL2 GLESv3 log)
