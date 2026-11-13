cmake_minimum_required(VERSION 3.22.1)
project(turok2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL2 desde el submódulo (fijado a release-2.32.10). Binary dir explícito: está fuera de src/.
# Opciones SDL_SHARED/SDL_STATIC/SDL_TEST se pasan desde android/app/build.gradle.kts.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/SDL2 ${CMAKE_BINARY_DIR}/SDL2)

# Debe ser SHARED: SDLActivity carga libturok2.so y busca SDL_main en ella
add_library(turok2 SHARED main.cpp)

# Flags estrictos solo para nuestro código (no se imponen a SDL)
target_compile_options(turok2 PRIVATE
    -Wall -Wextra -Werror=format-security
    -fstack-protector-strong
    $<$<CONFIG:Release>:-D_FORTIFY_SOURCE=2>)

target_link_libraries(turok2 PRIVATE SDL2::SDL2 GLESv3 log)
