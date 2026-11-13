cmake_minimum_required(VERSION 3.22.1)
project(turok2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pegamento de NativeActivity incluido en el NDK
add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue)

add_library(turok2 SHARED main.cpp)

target_compile_options(turok2 PRIVATE
    -Wall -Wextra -Werror=format-security
    -fstack-protector-strong
    $<$<CONFIG:Release>:-D_FORTIFY_SOURCE=2>)

# Exporta ANativeActivity_onCreate (si no, el linker lo descarta de la lib estática)
target_link_options(turok2 PRIVATE -u ANativeActivity_onCreate)

target_link_libraries(turok2 PRIVATE native_app_glue android log)

# TODO(SDL2): add_subdirectory(third_party/SDL2) y target_link_libraries(turok2 PRIVATE SDL2)
