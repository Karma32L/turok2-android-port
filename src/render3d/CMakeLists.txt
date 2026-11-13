# Núcleo 3D independiente de GL/SDL (parsers .map/.staticmesh/.kmat, construcción del nivel,
# matemática y cámara): se compila igual para Android y para los tests del host.
# El renderer GLES (gl/) se compila solo en la app (src/CMakeLists.txt).
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/Strict.cmake)

add_library(turok2_render3d_core STATIC
    kexbin.cpp
    map_file.cpp
    static_mesh.cpp
    kmat.cpp
    level_builder.cpp
    fly_camera.cpp)
target_include_directories(turok2_render3d_core PUBLIC ${CMAKE_CURRENT_LIST_DIR})
# turok2_vfs: normalizePath y lectura de archivos; turok2_render_core: text_parse.hpp
target_link_libraries(turok2_render3d_core PUBLIC turok2_vfs PRIVATE turok2_render_core)
set_target_properties(turok2_render3d_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
turok2_strict(turok2_render3d_core)
